// SPDX-FileCopyrightText: 2026 Friedrich-Alexander University Erlangen-Nuernberg (FAU), Computer Science 7 - Computer Networks and Communication Systems
// SPDX-License-Identifier: LGPL-3.0-or-later

/*
 * TcpBBRv1.cc
 *
 *  Created on: Apr 10, 2024
 *      Author: Luigi Martino
 */

#include <algorithm>    // min,max

#include "inet/transportlayer/tcp/flavours/TcpBBRv1.h"



#include "inet/common/INETMath.h"
#include "inet/transportlayer/tcp/Tcp.h"
#include "inet/transportlayer/tcp/flavours/TcpNewReno.h"
#include "inet/transportlayer/tcp/TcpSendQueue.h"

#include <omnetpp.h>
#include <cmath>
#include <cstring>  // for std::memset
#include <cstddef>  // for std::offsetof
#include <cstdint>  // for uint32_t_t and uint64_t

#include <assert.h>
#include <string.h>


#define BBR_LOG_MODE(MSG) \
    EV_INFO << "[BBR] " << MSG << ": Time: " << simTime() \
            << " | Mode: " << modeName(TcpBBRv1TestStateVariables::BbrMode_t mode) \
            << " | Cycle: " << state->m_cycleIndex << "\n"




/**
* @brief The number of phases in the BBR ProbeBW gain cycle.
 */
static const uint8_t GAIN_CYCLE_LENGTH = 8;

/**
* @brief BBR uses an eight-phase cycle with the given pacing_gain value
* in the BBR ProbeBW gain cycle.
*/
const static double PACING_GAIN_CYCLE[] = {5.0 / 4, 3.0 / 4, 1, 1, 1, 1, 1, 1};


using namespace tcp;
using namespace omnetpp;
using namespace inet;

Register_Class(TcpBBRv1);

// in initialize()
simsignal_t TcpBBRv1::bbrModeSignal = cComponent::registerSignal("bbrMode");    // BBR Mode state
simsignal_t TcpBBRv1::inflightSignal = cComponent::registerSignal("bbrBytesInFlight");    // BBR Mode state
simsignal_t TcpBBRv1::maxBwSignal = cComponent::registerSignal("bbrMaxBw");    // BBR Mode state
simsignal_t TcpBBRv1::minRttSignal = cComponent::registerSignal("bbrMinRtt");    // BBR Mode state





TcpBBRv1StateVariables::TcpBBRv1StateVariables()
{
    ssthresh = 65535;  //65535;


}

TcpBBRv1StateVariables::~TcpBBRv1StateVariables()
{
}

std::string TcpBBRv1StateVariables::str() const
{
    std::stringstream out;
    out << TcpBaseAlgStateVariables::str();
    out << " ssthresh=" << ssthresh;
    return out.str();
}

std::string TcpBBRv1StateVariables::detailedInfo() const
{
    std::stringstream out;
    out << TcpBaseAlgStateVariables::detailedInfo();
    out << "ssthresh = " << ssthresh << "\n";
//    out << "baseRTT = " << v_baseRTT << "\n";
    return out.str();
}


TcpBBRv1::TcpBBRv1()
    : TcpBaseAlg(), state((TcpBBRv1StateVariables *&)TcpAlgorithm::state)
{
}



static const char* modeName(TcpBBRv1StateVariables::BbrMode_t mode) {
    switch (mode) {
        case TcpBBRv1StateVariables::BBR_STARTUP: return "STARTUP";
        case TcpBBRv1StateVariables::BBR_DRAIN:   return "DRAIN";
        case TcpBBRv1StateVariables::BBR_PROBE_BW: return "PROBE_BW";
        case TcpBBRv1StateVariables::BBR_PROBE_RTT: return "PROBE_RTT";
        default: return "UNKNOWN";
    }
}



void TcpBBRv1::initialize() {
    TcpBaseAlg::initialize();


//    pacingTimer = new cMessage("PacingTimer");

    // Initialize BBR-specific variables
    state->m_pacingGain = 2.885; //1.0;
    state->m_cWndGain = 2.0;   // 1.0;
    state->m_highGain = 2.89;
    state->m_ssThresh = state->ssthresh;


    // RTT props
    state->m_minRtt = SimTime::getMaxTime();
    state->m_lastRtt = SimTime::getMaxTime();
    state->m_minRttStamp = SIMTIME_ZERO;
    state->m_minRttExpired = false;

    state->bdp_ = 0.0;
    state->sentBytes = 0;
    state->ack_cnt = 0;


    // Initialize BBR-specific variables

    state->stream = 4;

    state->m_priorCwnd = state->snd_cwnd;
    state->m_targetCWnd = state->snd_cwnd;

    // cwnd/pacing base params
    state->m_initialCWnd = 10 * state->snd_mss;
    state->snd_cwnd = state->m_initialCWnd;
    state->m_minPipeCwnd = 4 * state->snd_mss;
    state->m_sendQuantum = 1 * state->snd_mss;

    // Filters/windows
    state->m_bandwidthWindowLength = 10;  // in *rounds* if you use roundCount as timebase

    // Delivered accounting
    state->m_delivered = 0;
    state->lastDeliveredTime = SIMTIME_ZERO;

    // BBR init routines (ns-3 style)
    InitRoundCounting();
    InitFullPipe();
    EnterStartup();
    InitPacingRate(state);

    // ACK aggregation init
    state->m_ackEpochTime = simTime();
    state->m_ackEpochAcked = 0;
    state->m_extraAckedWinRtt = 0;
    state->m_extraAckedIdx = 0;
    state->m_extraAcked[0] = 0;
    state->m_extraAcked[1] = 0;

    state->m_isInitialized = true;


}


uint32_t TcpBBRv1::GetSsThresh(TcpBBRv1StateVariables *& state) //, uint32_t bytesInFlight) 
{
    EV << "GetSsThresh: " << state << endl;
//    uint32_t flight_size = std::min(state->snd_cwnd, state->snd_wnd);
    SaveCwnd(state);

//    state->m_bytesInFlight = InFlight(state, state->m_cWndGain);      //flight_size;

    return state->m_ssThresh;
}




void TcpBBRv1::SetStream(uint32_t stream)  
{
    EV << "Stream: " << stream << "/n";
    state->m_rngStream = stream;
//    std::cout<< "BBR: m_rngStream = "<< state->m_rngStream << std::endl;
}


void TcpBBRv1::InitRoundCounting()
{
    EV << "State: " << state << "/n";
    state->m_nextRoundDelivered = 0;
    state->m_roundStart = false;
    state->m_roundCount = 0;
}

void TcpBBRv1::InitFullPipe()
{
    EV << "State: " << state << "/n";
    state->m_isPipeFilled = false;
    state->m_fullBandwidth = 0;
    state->m_fullBandwidthCount = 0;
}




void TcpBBRv1::InitPacingRate(TcpBBRv1StateVariables *& state)
{
    if (!state->m_pacing)
        state->m_pacing = false;  // if false, will behave like BBR sending in Burst instead of pacing
    // Kick-start pacing if data exists
    if (!conn->isSendQueueEmpty())
        sendData(false);

    simtime_t rtt = state->m_minRtt;

    // If minRTT not known yet, assume 1ms (ns-3 uses 1ms fallback)
    if (rtt == SimTime::getMaxTime() || rtt <= SIMTIME_ZERO) {
        rtt = SimTime(1, SIMTIME_MS);
        state->m_hasSeenRtt = false;
    } else {
        state->m_hasSeenRtt = true;
    }

    double nominalBw = state->snd_cwnd / rtt.dbl();   // bytes/sec

    state->m_pacingRate = state->m_pacingGain * nominalBw;  // bytes/sec

    state->m_maxBwFilter = TcpBBRv1StateVariables::MaxBandwidthFilter_t(
        state->m_bandwidthWindowLength, nominalBw, state->m_roundCount);

    conn->emit(maxBwSignal, (double)state->m_maxBwFilter.GetBest());
}


void TcpBBRv1::SetPacingRate(TcpBBRv1StateVariables *& state, double gain)
{
    // Calculate the rate (bit/s)
    double  maxBw = state->m_maxBwFilter.GetBest(); 
    double rate = gain * maxBw;
    rate *= (1.0 - state->m_pacingMargin); // m_pacingMargin → pacing_margin in the struct

    // Clamp to maxPacingRate if there is, else skip this line
    if (state->m_maxPacingRate > 0)
        rate = std::min(rate, static_cast<double>(state->m_maxPacingRate));

    // Check if we haven't seen RTT and have a valid min RTT
    if (!state->m_hasSeenRtt && state->m_minRtt != SimTime::getMaxTime()) {
        InitPacingRate(state); // Implement this as needed
    }

    // Set the pacing rate only if pipe is filled or new rate is higher
    if (state->m_isPipeFilled || rate > state->m_pacingRate) {
        state->m_pacingRate = static_cast<uint64_t>(rate);
         EV_DEBUG << "Pacing rate updated. New value: " << state->m_pacingRate << endl;
    }

    debugBBR();
}



uint32_t TcpBBRv1::InFlight(TcpBBRv1StateVariables *& state, double gain)       
{
    EV_INFO <<"InFlight=" << state << "/n";
    if (state->m_minRtt == SIMTIME_MAX)
    {

        return state->m_initialCWnd;             
    }
    double quanta = 3 * state->m_sendQuantum;
    double estimatedBdp = state->m_maxBwFilter.GetBest() * state->m_minRtt.dbl(); 
    state->bdp_ = estimatedBdp;
    uint32_t inflight = (gain * estimatedBdp) + quanta;   

//    std::cout << "inFlight: " << inflight << std::endl;    // DEBUG

    if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_BW && state->m_cycleIndex == 0)
    {
        debugBBR();

          inflight += 2.0 * state->snd_mss;
    }

    // Clamp to minimum pipe cwnd (typically 4*MSS)
    inflight = std::max(inflight, state->m_minPipeCwnd);


    return  inflight; 
    debugBBR();

}



void TcpBBRv1::EnterStartup()
{
    EV_DEBUG << "EnterStartup: " << state << "\n";
//    BBR_LOG_MODE("EnterStartup()");

    SetBbrState(state->BbrMode_t::BBR_STARTUP);
//    std::cout<< "Entered BBR_STARTUP mode at time T = " << simTime() << std::endl;         // DEBUG

    state->m_pacingGain = state->m_highGain;
    state->m_cWndGain = state->m_highGain;
}





void TcpBBRv1::HandleRestartFromIdle(TcpBBRv1StateVariables *& state)   
{
    EV_INFO <<"HandleRestartFromIdle state=" << state << "/n";          

    state->m_bytesInFlight = InFlight(state, state->m_cWndGain);       //flight_size;

    if (sendQueue == nullptr)
        EV_WARN << "sendQueue is null, when in HandleRestartFromIdle skipping pipe check.\n";
        return;                      // To avoid calling CheckAppStatus


    if (state->m_bytesInFlight == 0U && state->isAppLimited)  // Included also the check whether the rate sample is application-limited. 
    {                                                                
        state->m_idleRestart = true;
        if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_BW)
        {
            debugBBR();
            SetPacingRate(state, 1);
        }
    }

}




void TcpBBRv1::SetBbrState(TcpBBRv1StateVariables::BbrMode_t mode)  //   BbrMode_t mode
{
//    NS_LOG_FUNCTION(this << mode);
    EV_TRACE << "SetBbrState(BbrMode_t mode): state=" << state << "\n";

//    NS_LOG_DEBUG(Simulator::Now() << " Changing from " << BbrModeName[state->m_state] << " to "
//                                  << BbrModeName[mode]);
//    EV_TRACE << "Time=" << simTime() << " Changing from " << state->BbrModeName[state->m_state] << " to " << state->BbrModeName[mode]  << "\n";

    state->m_state = mode;

    conn->emit(bbrModeSignal, (long)state->m_state);   // step signal for mode
}

void TcpBBRv1::AdvanceCyclePhase()
{
    EV_TRACE << "AdvanceCyclePhase(): state=" << state << "\n";
    state->m_cycleStamp = simTime();
    state->m_cycleIndex = (state->m_cycleIndex + 1) % GAIN_CYCLE_LENGTH;
    state->m_pacingGain = PACING_GAIN_CYCLE[state->m_cycleIndex];
//    EV_INFO << "BBR: m_pacingGain = " << state->m_pacingGain << "\n";

    debugBBR();
}



bool TcpBBRv1::IsNextCyclePhase(TcpBBRv1StateVariables *& state)              
{
    EV_TRACE << "IsNextCyclePhase(): state=" << state << "\n";

    bool isFullLength = (simTime() - state->m_cycleStamp) > state->m_minRtt;
    if (state->m_pacingGain == 1)
    {
        return isFullLength;
    }
    else if (state->m_pacingGain > 1)
    {
        return isFullLength &&
               (state->m_bytesLoss > 0 || state->m_priorInFlight >= InFlight(state, state->m_pacingGain));     // FIXME TODO -: Correct eventually this by adding a call to the method receivedDuplicateAck oder etwas, and store from there in a new variable the m_bytesLoss and m_priorInFlight
    }
    else
    {
        return isFullLength || state->m_priorInFlight <= InFlight(state, 1);
    }
}


void TcpBBRv1::CheckCyclePhase(TcpBBRv1StateVariables *& state)  
{
    EV_TRACE << "CheckCyclePhase(): state=" << state << "\n";

    if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_BW && IsNextCyclePhase(state))    // , rs
    {
        AdvanceCyclePhase();
        debugBBR();
    }
}


void TcpBBRv1::UpdateAppLimited()
{
    if (state->isAppLimited) {
        // as delivered increases, we approach the threshold
        if (state->m_delivered >= state->m_appLimitedUntil) {
            state->isAppLimited = false;
            state->m_appLimitedUntil = 0;
        }
    }
}


void TcpBBRv1::SetAppLimited()
{
//    // Assuming you also track delivered bytes in the state class:
//    state->m_appLimited = state->m_bytesInFlight; // std::max<uint32_t>(state->m_delivered + bytesInFlight, 1U);
//    state->isAppLimited = true;

        // Mark app-limited until we have delivered what was in flight at this point.
        uint32_t inflight = state->snd_max - state->snd_una;

        // delivered is bytes cumulatively acked by the sender (BBR delivered)
        state->m_appLimitedUntil = state->m_delivered + inflight;
        if (state->m_appLimitedUntil == 0) state->m_appLimitedUntil = 1;

        state->isAppLimited = true;

}



void TcpBBRv1::CheckFullPipe(TcpBBRv1StateVariables *& state)   
{
    EV_TRACE << "CheckFullPipe(): state=" << state << "\n";


    if (state->m_isPipeFilled || !state->m_roundStart || state->isAppLimited)  //  rs.m_isAppLimited           
    {
//        std::cout << "Are you here?: " << std::endl;
        return;
    }

    /* Check if Bottleneck bandwidth is still growing*/
    if (state->m_maxBwFilter.GetBest() >= state->m_fullBandwidth * 1.25)  
    {
        state->m_fullBandwidth = state->m_maxBwFilter.GetBest();
        conn->emit(maxBwSignal, (double)state->m_maxBwFilter.GetBest());

//        std::cout << "FullBandwidth in Conditional statement: " << state->m_fullBandwidth << std::endl;
        state->m_fullBandwidthCount = 0;
        return;
    }

    state->m_fullBandwidthCount++;
 //   std::cout << "FullBandwidthCount: " << state->m_fullBandwidthCount << std::endl;

    if (state->m_fullBandwidthCount >= 3)
    {

        EV_TRACE << "Pipe filled" << "\n";
        state->m_isPipeFilled = true;
    }
}




void TcpBBRv1::EnterDrain()
{
    EV_TRACE << "EnterDrain(): state=" << state << "\n";
//    BBR_LOG_MODE("EnterDrain()");

    SetBbrState(state->BbrMode_t::BBR_DRAIN);
//    std::cout<< "Entered BBR_DRAIN mode at time t = " << simTime() << std::endl;         // DEBUG

    state->m_pacingGain = 1.0 / state->m_highGain;
    state->m_cWndGain = state->m_highGain;
    debugBBR();
}




void TcpBBRv1::EnterProbeBW()
{
//    NS_LOG_FUNCTION(this);
    EV_TRACE << "EnterProbeBW(): state=" << state << "\n";

    SetBbrState(state->BbrMode_t::BBR_PROBE_BW);
//    std::cout<< "Entered BBR_PROBE_BW mode at time T =" << simTime() << std::endl;         // DEBUG

    state->m_pacingGain = 1;
    state->m_cWndGain = 2;

//    std::cout<< "conn pointer =  "<< conn << std::endl;
    ASSERT(conn != nullptr);

    cRNG *rng = conn->getRNG(state->m_rngStream);
//    std::cout<< "rng pointer =  "<< rng << std::endl;
    ASSERT(rng != nullptr);

    int rand_val = intuniform(rng, 0, GAIN_CYCLE_LENGTH - 2);  


    state->m_cycleIndex = GAIN_CYCLE_LENGTH - 1 - rand_val;

//    EV_INFO << "BBR: m_cycleIndex = " << state->m_cycleIndex << "\n";


    ASSERT(state->m_cycleIndex >= 0 && state->m_cycleIndex < GAIN_CYCLE_LENGTH);

    AdvanceCyclePhase();
    debugBBR();
}



void TcpBBRv1::CheckDrain(TcpBBRv1StateVariables *& state)
{
    EV_TRACE << "CheckDrain(): state=" << state << "\n";
    if (state->m_state == TcpBBRv1StateVariables::BBR_STARTUP && state->m_isPipeFilled)   //   ||
    {

        EnterDrain();
        state->m_ssThresh = InFlight(state, 1);
        debugBBR();

    }

    state->m_bytesInFlight = state->snd_max - state->snd_una;   
    // Track the actual/current Bytes in Flight via the state->m_bytesInFlight                                                          


    if (state->m_state == TcpBBRv1StateVariables::BBR_DRAIN && state->m_bytesInFlight <= InFlight(state, 1))  
    {
        debugBBR();
        EnterProbeBW();
    }
}




void TcpBBRv1::ExitProbeRTT()
{
    EV_TRACE << "ExitProbeRTT(): state=" << state << "\n";

    if (state->m_isPipeFilled)
    {
        EnterProbeBW();
       // std::cout<< "Entered ProbeBW from ExitProbeRTT, at time T = " << simTime() << std::endl;         // DEBUG
    }
    else
    {
        EnterStartup();
       // std::cout<< "Entered StartUP from ExitProbeRTT, at time T = " << simTime() << std::endl;         // DEBUG
    }

    EV_INFO << "ExitProbeRTT t=" << simTime()
            << " probeRttRoundDone=" << state->m_probeRttRoundDone
            << " doneStamp=" << state->m_probeRttDoneStamp
            << " inflight=" << (state->snd_max - state->snd_una)
            << "\n";
}








uint32_t TcpBBRv1::GetBbrState()
{
    EV_TRACE << "GetBbrState(): state=" << state << "\n";
    return state->m_state;
}

double TcpBBRv1::GetCwndGain()
{
    EV_TRACE << "GetCwndGain(): state=" << state << "\n";
    return state->m_cWndGain;
}



double TcpBBRv1::GetPacingGain()
{
    EV_TRACE << "GetPacingGain(): state=" << state << "\n";
    return state->m_pacingGain;
}




void TcpBBRv1::CongControl(TcpBBRv1StateVariables *& state, uint32 newlyAcked)  
{
    EV_TRACE << "CongControl(): state=" << state << "\n";
    state->m_delivered += newlyAcked;   
    UpdateAppLimited();
    state->lastDeliveredTime = simTime();

    UpdateModelAndState(state);                
    UpdateControlParameters(state);            
}










void TcpBBRv1::EnterRecovery()
{
    if (state->lossRecovery)
        return;

    SaveCwnd(state);                          // This was previously defined in ReceiveDataAck();
    state->lossRecovery = true;
    state->m_packetConservation = true;

    // Update inflight snapshot
    state->m_bytesInFlight = state->snd_max - state->snd_una;

    // Packet conservation cwnd:
    // allow sending only what is ACKed (or at least 1 MSS)
    state->snd_cwnd = state->m_bytesInFlight + std::max(state->m_ackedSacked, state->snd_mss);

    conn->emit(cwndSignal, state->snd_cwnd);
}





void TcpBBRv1::EnterLossRto()
{
    state->afterRto = true;

    if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_RTT) {
        RestoreCwnd(state);
        ExitProbeRTT();

    }

    SaveCwnd(state);
    state->m_pacingGain = 1.0;
    state->snd_cwnd = state->m_minPipeCwnd;
    conn->emit(cwndSignal, state->snd_cwnd);

    state->lossRecovery = false;
    state->m_packetConservation = false;
}



void TcpBBRv1::OnTxStart(TcpBBRv1StateVariables *&state)
{
    state->m_idleRestart = true;
    state->m_ackEpochTime = simTime();
    state->m_ackEpochAcked = 0;

    if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_BW) {
        SetPacingRate(state, 1.0);
    }
    else if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_RTT) {
        if (state->m_probeRttRoundDone && simTime() > state->m_probeRttDoneStamp) {
            state->m_minRttStamp = simTime();
            RestoreCwnd(state);
            ExitProbeRTT();
        }
    }
}






//Process rexmit timer
void TcpBBRv1::processRexmitTimer(TcpEventCode& event)
{

    TcpBaseAlg::processRexmitTimer(event);
    if (event == TCP_E_ABORT)
        return;

    // mark bytes lost 
    uint32 outstanding = state->snd_max - state->snd_una;
    state->m_bytesLoss += outstanding;

    EnterLossRto();

    if (pacingTimer->isScheduled())
        conn->cancelEvent(pacingTimer);

    // Retransmit first unacked segment
    conn->retransmitOneSegment(true);

    debugBBR();
}





void TcpBBRv1::segmentRetransmitted(uint32 fromseq, uint32 toseq)
{
    TcpBaseAlg::segmentRetransmitted(fromseq, toseq);

    state->regions.set(fromseq, toseq, simTime(),
            state->m_delivered,
            state->lastDeliveredTime);
}



bool TcpBBRv1::trySendOnePacedSegmentOrSchedule(bool sendCommandInvoked)
{


    // If pacing disabled or pacing rate invalid → fallback
    if (!state->m_pacing || state->m_pacingRate <= 0)
        return TcpBaseAlg::sendData(sendCommandInvoked);

    // If we are in loss recovery or RTO → do NOT pace
    if (state->lossRecovery || state->afterRto)
        return TcpBaseAlg::sendData(sendCommandInvoked);

    // If explicitly commanded (e.g., window reopened) → allow immediate send
    if (sendCommandInvoked && !pacingTimer->isScheduled())
        return TcpBaseAlg::sendData(sendCommandInvoked);


    // If pacing timer already scheduled for the future, do nothing now
    if (pacingTimer->isScheduled() && pacingTimer->getArrivalTime() > simTime()){
        return false;
    }

    if (conn->isSendQueueEmpty()){
        if (pacingTimer->isScheduled())
            conn->cancelEvent(pacingTimer);

        return false;
    }

    // Compute send budget from cwnd/wnd and inflight
    uint32_t inflight = state->snd_max - state->snd_una;
    uint32_t sendLimit = std::min(state->snd_cwnd, state->snd_wnd);

    if (sendLimit <= inflight + state->snd_mss) {
        // Not enough room for one full MSS (you can decide if you want to allow partial)
        return false;
    }

    uint32_t allowed = sendLimit - inflight;
    uint32_t toSend = std::min<uint32_t>(allowed, state->snd_mss);  // pace 1 MSS

    bool sent = conn->sendData(/*fullSegmentsOnly=*/true, toSend);
    if (!sent)
        return false;

    if (!conn->isSendQueueEmpty()) {
        // bytes/sec pacingRate -> seconds per MSS
        double interval = double(state->snd_mss) / state->m_pacingRate;
        interval = std::max(interval, 1e-6); 

        if (!pacingTimer->isScheduled())
            conn->scheduleAt(simTime() + interval, pacingTimer);
    }

    return true;
}



bool TcpBBRv1::sendData(bool sendCommandInvoked)
{
    bool wasIdle = (state->snd_una == state->snd_max);  // no inflight before sending
    bool hasData = !conn->isSendQueueEmpty();

    uint32_t inflight = state->snd_max - state->snd_una;
    uint32_t sendLimit = std::min(state->snd_cwnd, state->snd_wnd);

    // wouldSend means we have room in flight for at least 1 MSS
    bool wouldSend = (sendLimit > inflight + state->snd_mss);

    // Only consider app-limited if nothing to send
    if (conn->isSendQueueEmpty() && wouldSend) {
        SetAppLimited();
    }

    if (wasIdle && hasData) {
        OnTxStart(state);
    }

//    return TcpBaseAlg::sendData(sendCommandInvoked);
    if (state->m_pacing && state->m_pacingRate > 0.0)
        return trySendOnePacedSegmentOrSchedule(sendCommandInvoked);
    else
        return TcpBaseAlg::sendData(sendCommandInvoked);
}



void TcpBBRv1::dataSent(uint32_t fromseq) {
    TcpBaseAlg::dataSent(fromseq);

    // save the time the packet was sent
    // fromseq is the seq number of the 1st sent byte

    simtime_t sendtime = simTime();
    simtime_t priorDelivtime = SIMTIME_ZERO; //simTime();
    uint64_t deliv;

    state->regions.clearTo(state->snd_una);
//    state->regions.set(fromseq, state->snd_max, sendtime);

    state->regions.set(fromseq, state->snd_max, simTime(),
                       state->m_delivered,
                       state->lastDeliveredTime);
}



void TcpBBRv1::receivedDataAck(uint32 firstSeqAcked)
{
    // inflight before ACK processing (in ns-3 is priorInFlight)
       uint32_t inflight_before = state->snd_max - state->snd_una;
       state->m_priorInFlight = inflight_before;

       conn->emit(inflightSignal, (double)state->m_bytesInFlight);

       // base TCP updates snd_una, rtt estimator, dupacks, etc.
       TcpBaseAlg::receivedDataAck(firstSeqAcked);

       // Compute newly cumulatively ACKed bytes (guard against odd cases)
       uint32 newlyAcked = 0;
       if (seqGE(state->snd_una, firstSeqAcked))
           newlyAcked = state->snd_una - firstSeqAcked;

       if (newlyAcked == 0)
           return;

       // bytes acked/sacked this ACK (if no SACK: just newlyAcked)
             state->m_ackedSacked = newlyAcked; // + sackedBytes if would be possible

             // Update bytes in flight after ACK
             state->m_bytesInFlight = state->snd_max - state->snd_una;

             conn->emit(inflightSignal, (double)state->m_bytesInFlight);

             // after RTO: clear after first forward progress
             if (state->afterRto && newlyAcked > 0) {
                 state->afterRto = false;
                 EV_DETAIL << "BBR: leaving afterRto on new ACK at " << simTime() << "\n";
             }


//        MANAGED IN MaybeExitRecovery();
       // Exit fast recovery when ACK covers recover (RFC 3782)
       if (state->lossRecovery) {
           // If we haven't recovered everything up to 'recover', this is a partial ACK:
           if (!seqGE(state->snd_una - 1, state->recover)) {
               // retransmit next unacked segment to fill the hole(s)
               conn->retransmitOneSegment(false);
               restartRexmitTimer();
           } else {
               // Full ACK covers recover -> exit recovery
               state->lossRecovery = false;
               state->dupacks = 0;
//               OnRecoveryComplete(state);
                   state->m_packetConservation = false;
                   RestoreCwnd(state);

               // DEBUG
               EV_WARN << "Exit lossRecovery t=" << simTime()
                       << " snd_una=" << state->snd_una
                       << " recover=" << state->recover
                       << "\n";

           }
       }

       // find a transmit record for sampling
       const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
       if (found != nullptr) {
           simtime_t now = simTime();

           // RTT sample for this packet
           simtime_t rttSample = now - found->getLastSentTime();    

           // DEBUG
           if (rttSample > 1.0) { // 1 second threshold, tune as needed
               EV_WARN << "Large RTT sample t=" << simTime()
                       << " rttSample=" << rttSample
                       << " txCount=" << found->getTransmitCount()
                       << " lastSent=" << found->getLastSentTime()
                       << " snd_una=" << state->snd_una
                       << " snd_max=" << state->snd_max
                       << "\n";
           }

           // ns-3/Linux: sendElapsed vs ackElapsed, then interval = max(...)
           simtime_t sendElapsed = rttSample;
           simtime_t ackElapsed  = now - found->getPriorDeliveredTime();

           // guard against zero/negative
           if (ackElapsed <= SIMTIME_ZERO)
               ackElapsed = sendElapsed;

           state->m_interval = std::max(sendElapsed, ackElapsed);

           // Update RTprop (minRTT) and lastRTT
               state->m_lastRtt = rttSample;


           // For debugging consistency
           state->m_priorDelivered = found->getPriorDelivered();
       }

       else {
           // Fallback: still allow BBR to run, but sample will be weak
           state->m_interval = std::max(state->m_interval, SimTime(1, SIMTIME_MS));
       }

       // inflight after ACK (ns-3: bytesInFlight is maintained by TCP; we recompute)
       uint32_t inflight_after = state->snd_max - state->snd_una;
       state->m_bytesInFlight = inflight_after;
       conn->emit(inflightSignal, (double)state->m_bytesInFlight);

       // Main BBR control step (this should do: m_delivered += newlyAcked; UpdateModelAndState; UpdateControlParameters)
       CongControl(state, newlyAcked);

       // Delivery rate sample (ns-3: rs.delivered / rs.interval)
       // Use per-packet priorDelivered if available
       if (found != nullptr && state->m_interval > SIMTIME_ZERO) {

           uint64_t sampleDelivered = state->m_delivered - found->getPriorDelivered();         
           state->m_deliveryRate = sampleDelivered / state->m_interval.dbl(); // bytes/sec

       }

       conn->emit(cwndSignal, state->snd_cwnd);

       // Drain gate (only needed if pacing isn't enforced)
       uint32_t actual_inflight = state->snd_max - state->snd_una;
       uint32_t drain_target = InFlight(state, 1.0);


       sendData(false);
       debugBBR();

       // optional: cleanup old regions AFTER sampling
       state->regions.clearTo(state->snd_una);
}




void TcpBBRv1::receivedDuplicateAck()
{
        TcpBaseAlg::receivedDuplicateAck();

        // Enter recovery once
        if (!state->lossRecovery && state->dupacks == DUPTHRESH) {
            state->lossRecovery = true;
            state->recover = state->snd_max - 1;
            state->m_packetConservation = true;
            SaveCwnd(state);

            // If ProbeRTT, abort it
            if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_RTT) {
                RestoreCwnd(state);
                ExitProbeRTT();      // ExitProbeRTT should NOT jump to STARTUP (see below)

            }

            conn->retransmitOneSegment(false);
            restartRexmitTimer();
            return;
        }

        // In recovery: do not run normal model update here
        if (state->lossRecovery){

            EV_WARN << "ENTER RECOVERY t=" << simTime()
                    << " dupacks=" << state->dupacks
                    << " inflight=" << (state->snd_max - state->snd_una)
                    << " cwnd=" << state->snd_cwnd
                    << " recover=" << state->recover
                    << "\n";

            return;
        }

        sendData(false);
    }





double TcpBBRv1::calculateBDP() {                    
    // Implement BBR's BDP estimation logic here
    // Example: bdp_ = pacing_rate_ * min_rtt_
    double max_bw = state->m_maxBwFilter.GetBest();
    conn->emit(maxBwSignal, (double)state->m_maxBwFilter.GetBest());

    state->bdp_ = (max_bw * state->m_minRtt.dbl());
    debugBBR();

    return state->bdp_;         

}





void TcpBBRv1::EnterProbeRTT()
{
    EV_DETAIL << "EnterProbeRTT " << state << "\n";
//    BBR_LOG_MODE("EnterProbeRTT()");

    SetBbrState(state->BbrMode_t::BBR_PROBE_RTT);
//    std::cout<< "Entered BBR_PROBE_RTT at time t = " << simTime() << std::endl;         // DEBUG

    state->m_pacingGain = 1;
    state->m_cWndGain = 1;
//    debugBBR();

    EV_INFO << "EnterProbeRTT t=" << simTime()
            << " minRtt=" << state->m_minRtt
            << " minRttStamp=" << state->m_minRttStamp
            << " inflight=" << (state->snd_max - state->snd_una)
            << "\n";
}



void TcpBBRv1::SaveCwnd(TcpBBRv1StateVariables *& state)
{
    EV_DETAIL << "SaveCwnd " << state << "\n";
    if (!state->lossRecovery && state->m_state != TcpBBRv1StateVariables::BBR_PROBE_RTT)
    {
        state->m_priorCwnd = state->snd_cwnd;
        conn->emit(cwndSignal, state->snd_cwnd);
    }
    else
    {
        state->m_priorCwnd = std::max(state->m_priorCwnd, state->snd_cwnd);   
        conn->emit(cwndSignal, state->snd_cwnd);
    }

    debugBBR();
}



void TcpBBRv1::RestoreCwnd(TcpBBRv1StateVariables *& state)
{
    state->snd_cwnd = std::max(state->m_priorCwnd, state->snd_cwnd);
    conn->emit(cwndSignal, state->snd_cwnd);

    EV_DETAIL << "RestoreCwnd " << state->snd_cwnd << "\n";

}




void TcpBBRv1::CheckProbeRTT(TcpBBRv1StateVariables *& state)    
{
    EV_DETAIL << "CheckProbeRTT " << state << "\n";

    // Don't enter ProbeRTT during loss recovery or after RTO    
    if (state->lossRecovery || state->afterRto)
        return;


    if (state->m_state != TcpBBRv1StateVariables::BBR_PROBE_RTT && state->m_minRttExpired && !state->m_idleRestart)
    {
        debugBBR();
        EnterProbeRTT();
        SaveCwnd(state);
        state->m_probeRttDoneStamp = SIMTIME_ZERO;
    }

    if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_RTT)
    {
        HandleProbeRTT(state);
    }

    if (state->m_delivered > 0)
    {
        state->m_idleRestart = false;
    }
}

void TcpBBRv1::SetSendQuantum(TcpBBRv1StateVariables *& state)
{
    EV_DETAIL << "SetSendQuantum " << state << "\n";
    state->m_sendQuantum = 1 * state->snd_mss;
}

void TcpBBRv1::UpdateTargetCwnd(TcpBBRv1StateVariables *& state)
{
//    EV_DETAIL << "UpdateTargetCwnd " << state << "\n";
    state->m_targetCWnd = InFlight(state, state->m_cWndGain) + AckAggregationCwnd();
    // Enforce minimum window
        state->m_targetCWnd = std::max(state->m_targetCWnd, state->m_minPipeCwnd);
        state->snd_cwnd = state->m_targetCWnd;
        conn->emit(cwndSignal, state->snd_cwnd);

    EV_DETAIL << "Updated m_targetCWnd: " << state->m_targetCWnd << "\n";

}


uint32_t TcpBBRv1::AckAggregationCwnd()
{
    EV_DETAIL << "AckAggregationCwnd " << state << "\n";
    uint32_t maxAggrBytes = 0; // MaxBW * 0.1 secs
    uint32_t aggrCwndBytes = 0;

    if (state->m_extraAckedGain && state->m_isPipeFilled)
    {
        // maxBw in bytes/sec
        double maxBw = state->m_maxBwFilter.GetBest();
        conn->emit(maxBwSignal, (double)state->m_maxBwFilter.GetBest());

        // 0.1 seconds worth of max bandwidth:
        maxAggrBytes = static_cast<uint32_t>(maxBw * 0.1);

        aggrCwndBytes = state->m_extraAckedGain * std::max(state->m_extraAcked[0], state->m_extraAcked[1]);
        aggrCwndBytes = std::min(aggrCwndBytes, maxAggrBytes);
    }
    return aggrCwndBytes;
}

void TcpBBRv1::UpdateAckAggregation(TcpBBRv1StateVariables *& state)    
{
    EV_DETAIL << "UpdateAckAggregation " << state << "\n";
    uint32_t expectedAcked;
    uint32_t extraAck;
    uint32_t epochProp;

    if (!state->m_extraAckedGain || state->m_ackedSacked <= 0 || state->m_delivered < 0)
    {
        return;
    }

    if (state->m_roundStart)
    {
        state->m_extraAckedWinRtt = std::min<uint32_t>(31, state->m_extraAckedWinRtt + 1);
        if (state->m_extraAckedWinRtt >= state->m_extraAckedWinRttLength)
        {
            state->m_extraAckedWinRtt = 0;
            state->m_extraAckedIdx = state->m_extraAckedIdx ? 0 : 1;
            state->m_extraAcked[state->m_extraAckedIdx] = 0;
        }
    }

    epochProp = simTime().inUnit(SIMTIME_S) - state->m_ackEpochTime.inUnit(SIMTIME_S);
    expectedAcked = state->m_maxBwFilter.GetBest() * epochProp; 
    conn->emit(maxBwSignal, (double)state->m_maxBwFilter.GetBest());

    if (state->m_ackEpochAcked <= expectedAcked ||
        (state->m_ackEpochAcked + state->m_ackedSacked >= state->m_ackEpochAckedResetThresh))
    {
        state->m_ackEpochAcked = 0;
        state->m_ackEpochTime = simTime();
        expectedAcked = 0;
    }

    state->m_ackEpochAcked = state->m_ackEpochAcked + state->m_ackedSacked;
    extraAck = state->m_ackEpochAcked - expectedAcked;
    extraAck = std::min(extraAck, state->snd_cwnd);

    if (extraAck > state->m_extraAcked[state->m_extraAckedIdx])
    {
        state->m_extraAcked[state->m_extraAckedIdx] = extraAck;
    }
}

bool TcpBBRv1::ModulateCwndForRecovery(TcpBBRv1StateVariables *& state)    
{
    EV_DETAIL << "ModulateCwndForRecovery " << state << "\n";
    if (state->m_bytesLoss > 0)
    {
        state->snd_cwnd = std::max((int)state->snd_cwnd - (int)state->m_bytesLoss, (int)state->snd_mss);
        conn->emit(cwndSignal, state->snd_cwnd);
    }

    if (state->m_packetConservation)
    {
        state->snd_cwnd = std::max(state->snd_wnd, state->m_bytesInFlight + state->m_ackedSacked);  
        conn->emit(cwndSignal, state->snd_cwnd);
        return true;
    }
    return false;
}

void TcpBBRv1::ModulateCwndForProbeRTT(TcpBBRv1StateVariables *& state)
{
    EV_DETAIL << "ModulateCwndForProbeRTT " << state << "\n";
    if (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_RTT)
    {
        state->snd_cwnd = std::min(state->snd_cwnd, state->m_minPipeCwnd);
        conn->emit(cwndSignal, state->snd_cwnd);
    }
    debugBBR();
}

void TcpBBRv1::SetCwnd(TcpBBRv1StateVariables *& state)         
{
    EV_DETAIL << "SetCwnd " << state << "\n";

    // Only update if any new data was acknowledged/SACKed
     if (!state->m_ackedSacked)
         goto done; //return;

    if (state->lossRecovery) 
    {
        if (ModulateCwndForRecovery(state))
        {
            goto done; //return;
        }
    }

    UpdateTargetCwnd(state);

    if (state->m_isPipeFilled)
    {
        state->snd_cwnd = std::min(state->snd_cwnd + (uint32_t)state->m_ackedSacked, state->m_targetCWnd);
        conn->emit(cwndSignal, state->snd_cwnd);
    }
    else if (state->snd_cwnd < state->m_targetCWnd || state->m_delivered < state->m_initialCWnd * state->snd_mss)
    {
        state->snd_cwnd = state->snd_cwnd + state->m_ackedSacked;
        conn->emit(cwndSignal, state->snd_cwnd);
    }
    state->snd_cwnd = std::max(state->snd_cwnd, state->m_minPipeCwnd);
    conn->emit(cwndSignal, state->snd_cwnd);

    EV_DETAIL << "Congestion window updated. New value:" << state->snd_cwnd << "\n";

    // If we're in PROBE_RTT, enforce special cwnd rules

  done:

  ModulateCwndForProbeRTT(state);



    EV_WARN << "PC_CWND t=" << simTime()
            << " inflight=" << (state->snd_max - state->snd_una)
            << " cwnd=" << state->snd_cwnd
            << "\n";
}

void TcpBBRv1::UpdateRound(TcpBBRv1StateVariables *& state)   
{
    EV_DETAIL << "UpdateRound " << state << "\n";


    if (state->m_priorDelivered >= state->m_nextRoundDelivered)
    {
        state->m_nextRoundDelivered = state->m_delivered;
        state->m_roundCount++;
        state->m_roundStart = true;
        state->m_packetConservation = false;
    }
    else
    {
        state->m_roundStart = false;
    }
}

void TcpBBRv1::UpdateBottleneckBandwidth(TcpBBRv1StateVariables *& state)   
{
    EV_DETAIL << "UpdateBottleneckBandwidth " << state << "\n";

    if (state->m_delivered < 0 || state->m_interval == SIMTIME_ZERO)
    {
        return;
    }

    UpdateRound(state);  // , rs

    if (state->m_deliveryRate >= state->m_maxBwFilter.GetBest() || !state->isAppLimited)
    {
        state->m_maxBwFilter.Update(state->m_deliveryRate, state->m_roundCount);  
        conn->emit(maxBwSignal, (double)state->m_maxBwFilter.GetBest());
    }
}

void TcpBBRv1::UpdateModelAndState(TcpBBRv1StateVariables *& state) 
{
    EV_DETAIL << "UpdateModelAndState " << state << "\n";
//    std::cout<< "Entered UpdateModelAndState, at time T = " << simTime() << std::endl;         // DEBUG
    UpdateBottleneckBandwidth(state);   // , rs
    UpdateAckAggregation(state);   // , rs
    CheckCyclePhase(state);
    CheckFullPipe(state);
    CheckDrain(state);
    UpdateRTprop(state);
    CheckProbeRTT(state);  //, rs
}

void TcpBBRv1::UpdateControlParameters(TcpBBRv1StateVariables *& state)  
{
    EV_DETAIL << "UpdateControlParameters " << state << "\n";
//    std::cout<< "Entered UpdateControlParameters, at time T = " << simTime() << std::endl;         // DEBUG
    SetPacingRate(state, state->m_pacingGain);
    SetSendQuantum(state);
    SetCwnd(state);   // , rs
}




void TcpBBRv1::HandleProbeRTT(TcpBBRv1StateVariables *& state)
{
    EV_DETAIL << "HandleProbeRTT " << state << "\n";

    if (state->m_probeRttDoneStamp == SIMTIME_ZERO && state->m_bytesInFlight <= state->m_minPipeCwnd)
    {
        state->m_probeRttDoneStamp = simTime() + state->m_probeRttDuration;
        state->m_probeRttRoundDone = false;
        state->m_nextRoundDelivered = state->m_delivered;
    }
    else if (!(state->m_probeRttDoneStamp == SIMTIME_ZERO))
    {
        if (state->m_roundStart)
        {
            state->m_probeRttRoundDone = true;
        }
        if (state->m_probeRttRoundDone && simTime() > state->m_probeRttDoneStamp)
        {
            state->m_minRttStamp = simTime();
            RestoreCwnd(state);

            ExitProbeRTT();
 //           std::cout<< "Exited ProbeRTT from HandleProbeRTT, at time T = " << simTime() << std::endl;         // DEBUG

            debugBBR();

        }
    }
    debugBBR();
}




void TcpBBRv1::UpdateRTprop(TcpBBRv1StateVariables *& state)
{
    EV_DETAIL << "UpdateRTprop " << state << "\n";
    state->m_minRttExpired = simTime() > (state->m_minRttStamp + state->m_minRttFilterLen);  

    if (state->m_lastRtt > SIMTIME_ZERO && (state->m_lastRtt <= state->m_minRtt || state->m_minRttExpired))   
    {

        state->m_minRtt = state->m_lastRtt;
        conn->emit(minRttSignal, state->m_minRtt.dbl());

        state->m_minRttStamp = simTime();
    }
}


void TcpBBRv1::debugBBR()
{
    EV_DETAIL
        << "\n==================== BBR DEBUG ====================\n"
        << "Time:               " << simTime() << "\n"
        << "BBR Mode:           " << state->m_state << " (" << TcpBBRv1StateVariables::BbrMode_t(state->m_state) << ")\n"
        << "Cycle Index:        " << state->m_cycleIndex << "\n"
        << "snd_cwnd:           " << state->snd_cwnd << "\n"
        << "ssthresh:           " << state->ssthresh << "\n"
        << "InFlight:           " << state->m_bytesInFlight << "\n"
        << "Max BW:             " << state->m_maxBwFilter.GetBest() << "\n" // (" << (isBitsPerSec ? "bits/s" : "bytes/s") << ")\n"
        << "Pacing Rate New:        " << state->m_pacingRate << " bytes/s\n"
        << "cwnd Gain:          " << state->m_cWndGain << "\n"
        << "pacing Gain:        " << state->m_pacingGain << "\n"
        << "Min RTT:            " << state->m_minRtt << " s\n"
        << "min_rtt_stamp:      " << state->min_rtt_stamp_ << "\n"
        << "BDP:                " << state->bdp_ << "\n"  //(state->m_maxBwFilter.GetBest() * state->m_minRtt.dbl()) << " bytes\n"
        << "Target CWND:        " << state->m_targetCWnd << "\n"
        << "Initial CWND:       " << state->m_initialCWnd << "\n"
        << "Min Pipe CWND:      " << state->m_minPipeCwnd << "\n"
        << "Delivered:          " << state->m_delivered << "\n"
        << "Delivery Rate:          " << state->m_deliveryRate << "\n"
        << "Prior InFlight:     " << state->m_priorInFlight << "\n"
        << "last RTT:           " << state->m_lastRtt << "\n"
        << "last Acked:         " << state->ackedBytes << "\n"
        << "ProbeRTT Mode?:     " << (state->m_state == TcpBBRv1StateVariables::BBR_PROBE_RTT ? "YES" : "NO") << "\n"
        << "Extra Acked Gain:           " << state->m_extraAckedGain << "\n"
        << "Is Pipe Filled:             " << (state->m_isPipeFilled ? "YES" : "NO") << "\n"
        << "Send Quantum:               " << state->m_sendQuantum << "\n"
        << "===============================================\n"
        << endl;
}





