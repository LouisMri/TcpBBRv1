/*
 * TcpBBR.cc
 *
 *  Created on: Apr 10, 2024
 *      Author: martino
 */


#include <algorithm>    // min,max

#include "inet/transportlayer/tcp/flavours/TcpBBR.h"



#include "inet/common/INETMath.h"
#include "inet/transportlayer/tcp/Tcp.h"
#include "inet/transportlayer/tcp/flavours/TcpNewReno.h"
#include "inet/transportlayer/tcp/TcpSendQueue.h"

#include <omnetpp.h>
#include <cmath>
#include <cstring>  // for std::memset
#include <cstddef>  // for std::offsetof
#include <cstdint>  // for uint32_t_t and uint64_t



#define BBR_LOG_MODE(MSG) \
    EV_INFO << "[BBR] " << MSG << ": Time: " << simTime() \
            << " | Mode: " << modeName(TcpBBRStateVariables::BbrMode_t mode) \
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

using namespace omnetpp;
using namespace inet;
using namespace tcp;

Register_Class(TcpBBR);


TcpBBRStateVariables::TcpBBRStateVariables()
{
    ssthresh = 65535;  //65535;


    // Initialize BBR-specific variables
//       bdp_ = 0.0;
//       pacing_gain_ = 2.885;
//       cwnd_gain_ = 2.0;
//       highGain = 2.89;
//       min_rtt_;   //srtt;      //_ != simTime()::getInvalid() ? srtt_.dbl() : std::numeric_limits<double>::infinity();     //srtt_
//       min_rtt_stamp_;
//       probe_rtt_done_stamp_;
//       probe_rtt_round_done_;
//       prior_cwnd_;
//       idle_restart_;
//       extra_acked_interval_start_;
//       extra_acked_delivered_;
//       probe_rtt_mode_ = false;
//       max_bw_ = 0.0;
//       round_count_ = 0;




}

TcpBBRStateVariables::~TcpBBRStateVariables()
{
}

std::string TcpBBRStateVariables::str() const
{
    std::stringstream out;
    out << TcpBaseAlgStateVariables::str();
    out << " ssthresh=" << ssthresh;
    return out.str();
}

std::string TcpBBRStateVariables::detailedInfo() const
{
    std::stringstream out;
    out << TcpBaseAlgStateVariables::detailedInfo();
    out << "ssthresh = " << ssthresh << "\n";
//    out << "baseRTT = " << v_baseRTT << "\n";
    return out.str();
}


TcpBBR::TcpBBR()
    : TcpBaseAlg(), state((TcpBBRStateVariables *&)TcpAlgorithm::state)
{
}



//static const char* modeName(TcpBBRStateVariables::BbrMode_t mode) {
//    switch (mode) {
//        case TcpBBRStateVariables::BBR_STARTUP: return "STARTUP";
//        case TcpBBRStateVariables::BBR_DRAIN:   return "DRAIN";
//        case TcpBBRStateVariables::BBR_PROBE_BW: return "PROBE_BW";
//        case TcpBBRStateVariables::BBR_PROBE_RTT: return "PROBE_RTT";
//        default: return "UNKNOWN";
//    }
//}



void TcpBBR::initialize() {
    TcpBaseAlg::initialize();

    // Initialize BBR-specific variables
    state->m_pacingGain = 2.885; //1.0;
//    state->cwnd_gain_ = 2.0;   // 1.0;
    state->m_cWndGain = 2.0;   // 1.0;
    state->m_highGain = 2.89;


//    state->min_rtt_ = SIMTIME_MAX.dbl(); //std::numeric_limits<double>::max();
    state->bdp_ = 0.0;
//    state->full_bw_ = 0.0;
//    state->max_bw_ = 0.0;
//    state->probe_rtt_mode_ = false;
//    pacing_rate_prev_ = 0.0;
//    last_packet_sent_time_ = 0.0;

    // === Initial pacing rate ===
        // Must be > 0! Otherwise TCP will never send data.
        // Linux: initial pacing rate = cwnd_gain * initial_cwnd / RTT
        // But RTT is not known yet → use a safe fallback
//    state->pacing_rate_ = 1.0 * state->snd_mss;   // minimal safe pacing rate

    // Initialize BBR-specific variables

    state->m_minRtt = SIMTIME_MAX; //(state->srtt > simTime()) ? state->srtt.dbl() : std::numeric_limits<double>::infinity();   // Time::getInvalid() ? state->srtt.dbl() : std::numeric_limits<double>::infinity()    //state->srtt != Time::getInvalid() ? state->srtt.dbl() : std::numeric_limits<double>::infinity();     //srtt_
//    state->min_rtt_stamp_ = simTime();
//    state->probe_rtt_done_stamp_ = SIMTIME_ZERO;
//    state->probe_rtt_round_done_ = false;
//    state->prior_cwnd_ = 0;
//    state->idle_restart_ = false;
//    state->extra_acked_interval_start_ = simTime();
//    state->extra_acked_delivered_ = 0;
//    state->round_count_ = 0;
    state->stream = 4;
    state->m_minPipeCwnd = 4*state->snd_mss;
    state->m_initialCWnd = 10 * state->snd_mss;
    state->snd_cwnd = state->m_initialCWnd;
    state->m_bandwidthWindowLength = 10;state->m_initialCWnd;

    state->sentBytes = 0;
    state->m_bytesInFlight = state->m_initialCWnd;
    state->ack_cnt = 0;

//    state->m_pacing = true;

//    state->m_uv = getRNG(0);                    // hier war es die fehler auf die getRNG(0) Methode
//    int x = intuniform(0, 6, rng); // 0 = RNG index configured in omnetpp.ini
//
//    state->m_uv = new cUniform("m_uv", 0.0, 6.0);

    // make the module own it so it will be deleted automatically
//    take(state->m_uv); // optional but recommended to avoid manual delete


       // Call BBR-specific initialization functions to be implemented

//       BBRResetCongestionSignals();
//       BBRResetLowerBounds();
//       BBRInitRoundCounting();
//       BBRInitFullPipe();
//       BBRInitPacingRate();
//       BBREnterStartup();

    sendQueue = check_and_cast<TcpSendQueue*>(conn->getSendQueue());


    // Set initial BBR state and variables
    InitPacingRate(state);

    // === Enter BBR STARTUP mode ===
    EnterStartup();

    // === Initialize round counting ===
    InitRoundCounting();


    InitFullPipe();

//    EnterStartup();
}



uint32_t TcpBBR::GetSsThresh(TcpBBRStateVariables *& state, uint32_t bytesInFlight)  // should maybe this method receive gain as input???
{
    EV << "GetSsThresh: " << bytesInFlight << endl;
//    uint32_t flight_size = std::min(state->snd_cwnd, state->snd_wnd);
    state->m_bytesInFlight = InFlight(state, state->m_cWndGain);      //flight_size;

    return state->m_ssThresh;
}


//void TcpBBR::SetRateOps(Ptr<TcpRateOps> rateOps)
//{
//    state->m_rateOps = rateOps;
//}
//
//const char* const TcpBBR::BbrModeName[BBR_PROBE_RTT + 1] = {
//    "BBR_STARTUP",
//    "BBR_DRAIN",
//    "BBR_PROBE_BW",
//    "BBR_PROBE_RTT",
//};


void TcpBBR::SetStream(uint32_t stream)  // void, If defining this method as "void" then use the assignement of uint32_t = unit32_t
{
    EV << "Stream: " << stream << "/n";
//    uint32_t m_uv_stream = stream;
//    state->m_uv = conn->getRNG(stream); //getRNG(stream);
//   return  state->m_uv; // = SetStream(stream);
    state->m_rngStream = stream;
//    std::cout<< "BBR: m_rngStream = "<< state->m_rngStream << std::endl;
}


void TcpBBR::InitRoundCounting()
{
    EV << "State: " << state << "/n";
    state->m_nextRoundDelivered = 0;
    state->m_roundStart = false;
    state->m_roundCount = 0;
}

void TcpBBR::InitFullPipe()
{
    EV << "State: " << state << "/n";
    state->m_isPipeFilled = false;
    state->m_fullBandwidth = 0;
    state->m_fullBandwidthCount = 0;
}




//void TcpBBR::restoreCwnd(TcpBBRStateVariables *& state)
//{
//    // m_priorCwnd should be expressed in the same units as snd_cwnd (bytes).
//        // If your prior value is in segments, multiply by state->snd_mss first.
//        uint32_t priorBytes = state->m_priorCwnd; // ensure this is bytes (see note below)
//
//        // Restore to max(prior, current)
//        state->snd_cwnd = std::max(priorBytes, state->snd_cwnd);
//
//        EV_DETAIL << "TcpBbr::restoreCwnd(): snd_cwnd set to "
//                  << state->snd_cwnd << " (prior=" << priorBytes << ")\n";
//}


void TcpBBR::InitPacingRate(TcpBBRStateVariables *& state)   // Ptr<TcpSocketState> tcb
{
    EV_INFO <<"InitPacingRate state=" << state << "/n";

    if (!state->m_pacing)
    {
        EV_INFO <<"BBR must use pacing" << "/n";
        state->m_pacing = true; //true;
    }

    simtime_t Time;   // it is already defined in TcpBaseAlg.h

    if (state->m_minRtt != SIMTIME_MAX)   // state->m_minRtt  or state->rtt ?
    {
        Time = MilliSeconds(std::max<long int>(state->m_minRtt.inUnit(SIMTIME_MS), 1));    // .inUnit(SIMTIME_US)  GetMilliSeconds()
        state->m_hasSeenRtt = true;
    }
    else
    {
        Time = MilliSeconds(1);
//        state->m_minRtt = state->rtt;
    }



    // für nominalBandwidth, zwei optionen sind da, als datarate Datentyp definieren (und Datarate methode schreiben oder als "double" machen)
    double nominalBandwidth = (state->snd_cwnd) / Time.dbl();   // dies war der Datentyp DataRate typ in ns3, aber es ist wahrscheinlich besser, ihn als "double" zu definieren  *8
    state->m_pacingRate = state->m_pacingGain * nominalBandwidth;  //diese muss in (bit sein), also check später

    state->m_maxBwFilter = TcpBBRStateVariables::MaxBandwidthFilter_t(state->m_bandwidthWindowLength, nominalBandwidth, 0);
    // FIXME TODO -: Check and correct the usage of state->MaxBandwidthFilter_t

    debugBBR();

//    std::cout << "nominalBandwidth: " << nominalBandwidth << std::endl;
//    std::cout << "m_pacingRate: " << state->m_pacingRate << std::endl;
//    std::cout << "m_maxBwFilter in InitPacingRate (Best): " << state->m_maxBwFilter.GetBest() << std::endl;
}




void TcpBBR::SetPacingRate(TcpBBRStateVariables *& state, double gain)
{
    // Calculate the rate (bit/s)
    uint64_t maxBw = state->m_maxBwFilter.GetBest(); // You need to implement this
    double rate = gain * maxBw;
    rate *= (1.0 - state->m_pacingMargin); // m_pacingMargin → pacing_margin in your struct

    // Clamp to maxPacingRate if you have one, else skip this line, if PACING is not implemented it will not go here
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



uint32_t TcpBBR::InFlight(TcpBBRStateVariables *& state, double gain)       // renamed here: m_inflight with m_bytesInFlight
{
    EV_INFO <<"InFlight=" << state << "/n";
    if (state->m_minRtt == SIMTIME_MAX)
    {
//        state->m_bytesInFlight = state->snd_cwnd; // * state->m_initialCWnd;

        return state->m_initialCWnd;             //    (state->snd_cwnd * state->snd_mss)   ---> state->m_initialCWnd , Maybe:   10 * state->snd_mss
    }
    double quanta = 3 * state->m_sendQuantum;
    double estimatedBdp = state->m_maxBwFilter.GetBest() * state->m_minRtt.dbl(); // / 8.0;
    state->bdp_ = estimatedBdp;
    double inflight = (gain * estimatedBdp) + quanta;

    if (state->m_state == TcpBBRStateVariables::BBR_PROBE_BW && state->m_cycleIndex == 0)
    {
        debugBBR();
//        inflight += 2 * state->snd_mss;
//        state->m_bytesInFlight = (gain * estimatedBdp) + quanta + (2 * state->snd_mss);

        return (gain * estimatedBdp) + quanta + (2 * state->snd_mss); //state->m_bytesInFlight;
    }

//    state->m_bytesInFlight = (gain * estimatedBdp) + quanta;

    return inflight;   //(uint32_t)std::max(inflight, 4.0 * state->snd_mss);


}



void TcpBBR::EnterStartup()
{
    EV_DEBUG << "EnterStartup: " << state << "\n";
//    BBR_LOG_MODE("EnterStartup()");

    SetBbrState(state->BbrMode_t::BBR_STARTUP);
    std::cout<< "Entered BBR_STARTUP mode at time T = " << simTime() << std::endl;         // DEBUG

    state->m_pacingGain = state->m_highGain;
    state->m_cWndGain = state->m_highGain;
}





void TcpBBR::HandleRestartFromIdle(TcpBBRStateVariables *& state)   // , const TcpRateOps::TcpRateSample& rs
{
    EV_INFO <<"HandleRestartFromIdle state=" << state << "/n";
//    uint32 flight_size = state->snd_nxt - state->snd_una;           //  std::min(state->snd_cwnd, state->snd_wnd);   //

    state->m_bytesInFlight = InFlight(state, state->m_cWndGain);       //flight_size;

    if (sendQueue == nullptr)
        EV_WARN << "sendQueue is null, when in HandleRestartFromIdle skipping pipe check.\n";
        return;                      // To avoid calling CheckAppStatus

    CheckAppStatus();  // flight_size

    if (state->m_bytesInFlight == 0U && state->isAppLimited == 0)  // && rs.m_isAppLimited     FIXME TODO -: Include also the condition that checks: whether the rate sample is application-limited. Already Done!
    {
        state->m_idleRestart = true;
        if (state->m_state == TcpBBRStateVariables::BBR_PROBE_BW)
        {
            debugBBR();
            SetPacingRate(state, 1);
        }
    }

}


//void TcpBBR::SetPacingRate(TcpBBRStateVariables *& state, double gain)
//{
//    EV_TRACE << "updatePacing(): this=" << this << " state=" << state << " gain=" << gain << "\n";
//
//
//    double rate = gain * state->m_maxBwFilter.GetBest().GetBitRate(); // <-- bit/s
//
//    rate *= (1.0 - state->m_pacingMargin);     // keep same arithmetic, but use double math
//
//    rate = std::min(rate, state->m_maxPacingRate);
//
//    if (!state->m_hasSeenRtt && state->m_minRtt != SIMTIME_MAX)
//    {
//        InitPacingRate(state);
//    }
//
//    if (state->m_isPipeFilled || rate > state->m_pacingRate)
//    {
//        state->m_pacingRate = rate;
//        EV_DEBUG << "Pacing rate updated. New value: " << state->m_pacingRate << " bps\n";
//        // NS_LOG_DEBUG("Pacing rate updated. New value: " << state->m_pacingRate);
//    }
//}

void TcpBBR::SetBbrState(TcpBBRStateVariables::BbrMode_t mode)  //   BbrMode_t mode
{
//    NS_LOG_FUNCTION(this << mode);
    EV_TRACE << "SetBbrState(BbrMode_t mode): state=" << state << "\n";

//    NS_LOG_DEBUG(Simulator::Now() << " Changing from " << BbrModeName[state->m_state] << " to "
//                                  << BbrModeName[mode]);
//    EV_TRACE << "Time=" << simTime() << " Changing from " << state->BbrModeName[state->m_state] << " to " << state->BbrModeName[mode]  << "\n";

    state->m_state = mode;
}

void TcpBBR::AdvanceCyclePhase()
{
    EV_TRACE << "AdvanceCyclePhase(): state=" << state << "\n";
    state->m_cycleStamp = simTime();
    state->m_cycleIndex = (state->m_cycleIndex + 1) % GAIN_CYCLE_LENGTH;
    state->m_pacingGain = PACING_GAIN_CYCLE[state->m_cycleIndex];
//    EV_INFO << "BBR: m_pacingGain = " << state->m_pacingGain << "\n";
    std::cout<< "BBR: m_pacingGain = "<< state->m_pacingGain << std::endl;     // Debug
    debugBBR();
}



bool TcpBBR::IsNextCyclePhase(TcpBBRStateVariables *& state)              //  , const TcpRateOps::TcpRateSample& rs
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
               (state->m_bytesLoss > 0 || state->m_priorInFlight >= InFlight(state, state->m_pacingGain));     // FIXME TODO -: Correct this by adding a call to the method receivedDuplicateAck or etwas, and store from there in a new variable the m_bytesLoss and m_priorInFlight
    }
    else
    {
        return isFullLength || state->m_priorInFlight <= InFlight(state, 1);
    }
}
   // Frage: in which phase/states of a tcp connection should i record the m_bytesLoss and the m_priorInFlight, considering the standard phases like here:


void TcpBBR::CheckCyclePhase(TcpBBRStateVariables *& state)   // , const TcpRateOps::TcpRateSample& rs
{
    EV_TRACE << "CheckCyclePhase(): state=" << state << "\n";

    if (state->m_state == TcpBBRStateVariables::BBR_PROBE_BW && IsNextCyclePhase(state))    // , rs
    {
        AdvanceCyclePhase();
        debugBBR();
    }
}


void TcpBBR::SetAppLimited(uint32_t bytesInFlight)
{
    // Assuming you also track delivered bytes in the state class:
    state->m_appLimited = std::max<uint32_t>(state->m_delivered + bytesInFlight, 1U);
    // You can emit a signal/trace here, as in ns-3
    // emit(appLimitedSignal, state->appLimitedMarker);             // optional
}


//Checks if a flow is application-limited,which is if the sender cannot fill the congestion window because it does not have enough data from the application.
void TcpBBR::CheckAppStatus()
{
    uint32 fromSeq = state->snd_nxt;
    EV_DEBUG << "getBytesAvailable called with fromSeq=" << fromSeq << "\n";
//    EV_DEBUG << "getBufferEndSeq()=" << sendQueue->getBufferEndSeq() << "\n";
    if (sendQueue == nullptr) {
        EV_ERROR << "sendQueue is null in CheckAppStatus!\n";
        return;  // or handle error
    }

    if (sendQueue->getBytesAvailable(fromSeq) == 0) {
    // Application-limited: there is nothing left to send
        uint32 bytesInFlighing = state->snd_nxt - state->snd_una;   // To-Check
        SetAppLimited(bytesInFlighing);
        state->isAppLimited = true;
        std::cout<< "state->isAppLimited = True = "<< state->isAppLimited << std::endl;            // DEBUG
    }   else {
         state->isAppLimited = false;
         std::cout<< "state->isAppLimited = False = "<< state->isAppLimited << std::endl;            // DEBUG
    }
}


void TcpBBR::CheckFullPipe(TcpBBRStateVariables *& state)   //const TcpRateOps::TcpRateSample& rs
{
    EV_TRACE << "CheckFullPipe(): state=" << state << "\n";

//    if (sendQueue == nullptr)
//        EV_WARN << "sendQueue is null when in CheckFullPipe, skipping pipe check.\n";
//        return;

//    CheckAppStatus();  // might check if the object passed in this method is correct

    if (state->m_isPipeFilled || !state->m_roundStart || state->isAppLimited)  //  rs.m_isAppLimited            Already Done!
    {
        return;
    }

    /* Check if Bottleneck bandwidth is still growing*/
    if (state->m_maxBwFilter.GetBest() >= state->m_fullBandwidth * 1.25)  // .GetBitRate()
    {
        state->m_fullBandwidth = state->m_maxBwFilter.GetBest();
        state->m_fullBandwidthCount = 0;
        return;
    }

    state->m_fullBandwidthCount++;
    if (state->m_fullBandwidthCount >= 3)
    {

        EV_TRACE << "Pipe filled" << "\n";
        state->m_isPipeFilled = true;
        CheckDrain(state);   // added 21.01.26
    }
}




void TcpBBR::EnterDrain()
{
    EV_TRACE << "EnterDrain(): state=" << state << "\n";
//    BBR_LOG_MODE("EnterDrain()");

    SetBbrState(state->BbrMode_t::BBR_DRAIN);
    std::cout<< "Entered BBR_DRAIN mode at time t = " << simTime() << std::endl;         // DEBUG

    state->m_pacingGain = 1.0 / state->m_highGain;
    state->m_cWndGain = state->m_highGain;
    debugBBR();
}




void TcpBBR::EnterProbeBW()
{
//    NS_LOG_FUNCTION(this);
    EV_TRACE << "EnterProbeBW(): state=" << state << "\n";
//    BBR_LOG_MODE("EnterProbeBW()");

//    if (state->m_uv == nullptr)                                         // To delete
//        throw cRuntimeError("BBR: RNG m_uv not initialized!");

    SetBbrState(state->BbrMode_t::BBR_PROBE_BW);
    std::cout<< "Entered BBR_PROBE_BW mode" << std::endl;         // DEBUG

    state->m_pacingGain = 1;
    state->m_cWndGain = 2;

//    EV_INFO << "BBR: m_uv RNG pointer = " << state->m_uv << "\n";       // To delete
    std::cout<< "conn pointer =  "<< conn << std::endl;
    ASSERT(conn != nullptr);

    cRNG *rng = conn->getRNG(state->m_rngStream);
    std::cout<< "rng pointer =  "<< rng << std::endl;
    ASSERT(rng != nullptr);

    int rand_val = intuniform(rng, 0, GAIN_CYCLE_LENGTH - 2);  // 0-6    when initializing m_uv = nullptr this produce error.

//    EV_INFO << "BBR: m_uv RNG pointer = " << state->m_uv << "\n";                       // To delete
//    std::cout<< "BBR: m_uv RNG pointer =  "<< state->m_uv << std::endl;    // Debug     // To delete

    state->m_cycleIndex = GAIN_CYCLE_LENGTH - 1 - rand_val;

//    EV_INFO << "BBR: m_cycleIndex = " << state->m_cycleIndex << "\n";
    std::cout<< "BBR: m_cycleIndex =  "<< state->m_cycleIndex << std::endl;      // Debug

    if (state->m_cycleIndex < 0 || state->m_cycleIndex >= GAIN_CYCLE_LENGTH)
        state->m_cycleIndex = 0;

    AdvanceCyclePhase();
    debugBBR();
}



void TcpBBR::CheckDrain(TcpBBRStateVariables *& state)
{
    EV_TRACE << "CheckDrain(): state=" << state << "\n";
    if (state->m_state == TcpBBRStateVariables::BBR_STARTUP && state->m_isPipeFilled)   //   ||
    {

        EnterDrain();
        state->m_ssThresh = InFlight(state, 1);
        debugBBR();

    }

    uint32_t flight_size = state->snd_nxt - state->snd_una;   //std::min(state->snd_cwnd, state->snd_wnd);
    state->m_bytesInFlight = InFlight(state, state->m_cWndGain);               //flight_size;

    if (state->m_state == TcpBBRStateVariables::BBR_DRAIN && flight_size <= InFlight(state, 1))  // state->m_bytesInFlight
    {
        debugBBR();
        EnterProbeBW();
    }
}




void TcpBBR::ExitProbeRTT()
{
    EV_TRACE << "ExitProbeRTT(): state=" << state << "\n";

    if (state->m_isPipeFilled)
    {
        EnterProbeBW();
    }
    else
    {
        EnterStartup();
    }
}








uint32_t TcpBBR::GetBbrState()
{
    EV_TRACE << "GetBbrState(): state=" << state << "\n";
    return state->m_state;
}

double TcpBBR::GetCwndGain()
{
    EV_TRACE << "GetCwndGain(): state=" << state << "\n";
    return state->m_cWndGain;
}



double TcpBBR::GetPacingGain()
{
    EV_TRACE << "GetPacingGain(): state=" << state << "\n";
    return state->m_pacingGain;
}




void TcpBBR::CongControl(TcpBBRStateVariables *& state, uint32 newlyAcked)   // const TcpRateOps::TcpRateConnection& rc, const TcpRateOps::TcpRateSample& rs
{
    EV_TRACE << "CongControl(): state=" << state << "\n";
    state->m_delivered += newlyAcked;   // rc.m_delivered
    UpdateModelAndState(state);                // , rs
    UpdateControlParameters(state);            // , rs
}

void TcpBBR::CongestionStateSet(TcpBBRStateVariables *& state) //, TcpCAEvent_t& newState, TcpEventCode& event)     // const TcpSocketState::TcpCongState_t newState    ,  TcpCAEvent_t& newState
{
    EV_TRACE << "CongestionStateSet(): state=" << state << "\n";   // newState
//    std::cout << "Entered CongestionStateSet, at time T = " << simTime() << std::endl;

    if (!state->m_isInitialized)   //  event == TcpEventCode::TCP_E_OPEN_ACTIVE &&    ## newState == TcpSocketState::CA_OPEN   !state->lossRecovery
    {
        EV_TRACE << "CongestionStateSet triggered to CA_OPEN :: " << state << "\n";

        state->m_minRtt = state->rtt != SimTime::getMaxTime() ? state->rtt : SimTime::getMaxTime();
        state->m_minRttStamp = simTime();
        state->m_priorCwnd = state->snd_cwnd;
        state->m_ssThresh = state->ssthresh;                //m_initialSsThresh;
        state->m_targetCWnd = state->snd_cwnd;
        state->m_minPipeCwnd = 4 * state->snd_mss;
        state->m_sendQuantum = 1 * state->snd_mss;

        InitRoundCounting();
        InitFullPipe();
        EnterStartup();
        InitPacingRate(state);
        state->m_ackEpochTime = simTime();
        state->m_extraAckedWinRtt = 0;
        state->m_extraAckedIdx = 0;
        state->m_ackEpochAcked = 0;
        state->m_extraAcked[0] = 0;
        state->m_extraAcked[1] = 0;
        state->m_isInitialized = true;
    }
    else if (state->afterRto)  // state->lossRecovery ||    ## newState == CA_EVENT_LOSS
    {
        EV_TRACE << "CongestionStateSet triggered to CA_LOSS :: " << state << "\n";
        SaveCwnd(state);
        state->m_roundStart = true;
    }
    else if (state->lossRecovery)   // newState == TcpSocketState::CA_RECOVERY
    {
        EV_TRACE << "CongestionStateSet triggered to CA_RECOVERY :: " << state << "\n";
        SaveCwnd(state);
        state->snd_cwnd = state->m_bytesInFlight + std::max(state->m_ackedSacked, state->snd_mss);   // m_lastAckedSackedBytes instead of m_ackedSacked ?
        state->m_packetConservation = true;
    }
}







void TcpBBR::cwndEvent(TcpCAEvent_t& event, TcpConnection* conn, TcpBBRStateVariables *& state)  // bbr* state
{
    // Optionally use EV_INFO or EV_DEBUG for logging, or comment out
    // EV_INFO << "cwndEvent: " << event << endl;

    if (sendQueue == nullptr)
        EV_WARN << "sendQueue is null when in cwndEvent, skipping pipe check.\n";
        return;

    CheckAppStatus();   //state->snd_wnd

    if (event == CA_EVENT_COMPLETE_CWR) {
        // EV_DEBUG << "CwndEvent: CA_EVENT_COMPLETE_CWR: " << event << endl;
        state->m_packetConservation = false; // if you have this member, else ignore
        RestoreCwnd(state);           // implement or replace with your logic
    }
    else if (event == CA_EVENT_TX_START && state->isAppLimited == true) {       /* && app-limited logic here if available */   // && m_rateOps->GetConnectionRate().m_appLimited
        // ToDo: Add app-limited detection if you have it in your state or conn

        EV_DEBUG << "CwndEvent: CA_EVENT_TX_START: " << event << endl;
        state->m_idleRestart = 1;                                       // original: m_idleRestart = true;
        state->m_ackEpochTime = simTime().inUnit(SIMTIME_US);
        state->m_ackEpochAcked = 0;

        if (state->m_state  == TcpBBRStateVariables::BBR_PROBE_BW) {
            debugBBR();
            SetPacingRate(state, 1); // implement or call your method
        }
        else if (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT) {
            if (state->m_probeRttRoundDone && simTime().inUnit(SIMTIME_US) > state->m_probeRttDoneStamp.inUnit(SIMTIME_US)) {
                state-> m_minRttStamp = simTime().inUnit(SIMTIME_US);
                RestoreCwnd(state);
                ExitProbeRTT(); // implement or call your method
            }
        }
    }
}




//Process rexmit timer
void TcpBBR::processRexmitTimer(TcpEventCode& event)
{
    TcpBaseAlg::processRexmitTimer(event);

    if (event == TCP_E_ABORT)
        return;

    state->recover = (state->snd_max - 1);
    EV_INFO << "recover=" << state->recover << "\n";
    state->lossRecovery = false;
    state->firstPartialACK = false;
    EV_INFO << "Loss Recovery terminated.\n";




//     The pacing gain is a key concept in BBR, representing the rate at which packets are sent into the network. During normal operation,
//    the cwnd is not directly controlled by BBR; instead, BBR adjusts the pacing rate based on observed network conditions. However, when an RTX timeout occurs,
//    BBR reduces the cwnd to the pacing gain.

    // Implement BBR-specific logic for updating pacing rate and congestion window
//    updatePacingRate();   //method removed
//    state->snd_cwnd = state->m_minPipeCwnd;
//    state->m_pacingGain = 1.0;

//    SetPacingRate(state, state->m_pacingGain);
//    updateCongestionWindow(); // before was this now update to UpdateTargetCwnd

    state->m_ackedSacked = 0;



    conn->emit(cwndSignal, state->snd_cwnd);
    EV_DETAIL << "RXT Timeout in BBR: resetting cwnd to " << state->snd_cwnd << "\n"
              << ", ssthresh=" << state->ssthresh << "\n";

//    state->v_cwnd_changed = simTime();    // Save time when cwnd changes due to rtx

//    EV_INFO << "Entering StartUp: resetting cwnd and ssthresh=" << "\n";


    state->afterRto = true;

    CongestionStateSet(state);

//    state->snd_cwnd = state->m_initialCWnd;

    conn->retransmitOneSegment(true);    //retransmit one segment from snd_una

    debugBBR();
}


//void TcpBBR::receivedDataAck(uint32 firstSeqAcked) {  //const Ptr<const TcpHeader>& ack
//    TcpBaseAlg::receivedDataAck(firstSeqAcked);
//
//    state->ackedBytes = state->snd_una;
//    state->m_ackedSacked += state->ackedBytes + state->sackedBytes;
//
//    state->sentBytes += firstSeqAcked;   // sentBytes = m_deliveredBytes
//
//    simtime_t now = simTime();
//
//    simtime_t sendElapsed = (state->first_sent_time != SIMTIME_ZERO) ? (state->time_last_data_sent - state->first_sent_time) : SIMTIME_ZERO;
//    EV_DETAIL << "sendElapsed: " << sendElapsed << "\n";
//
//
//    if (state->time_firstAck == SIMTIME_ZERO) // not yet set for this interval
//         state->time_firstAck = simTime();
//     state->time_lastAck = simTime();
//
//    simtime_t ackElapsed = (state->time_lastAck != SIMTIME_ZERO) ? (now - state->time_lastAck) : sendElapsed;
//    EV_DETAIL << "ackElapsed: " << ackElapsed << "\n";
//
//    state->m_interval = std::max(sendElapsed, ackElapsed);
//
//    if (state->m_interval > SIMTIME_ZERO) {
//            state->m_deliveryRate = (double)state->sentBytes * 8 / state->m_interval.dbl(); // bytes per second ### with * 8 is bit/s ###
//        }
//
//
//
//    // Cache bytes in flight before processing this ACK
//        state->m_priorInFlight = state->snd_nxt - state->snd_una;
//
//
//    // Implementing tracking delivery in BBR:
////        uint32 ackNum = lastAckNo->getAckNo();     // Older version
////        state->lastAckNo = tcpHeader->getAckNo();
////        state->delivered = (state->lastAckNo >= state->iss) ? (state->lastAckNo - state->iss) : 0;
//
//        // 2. Update delivered counters
//        uint32_t newlyAcked = firstSeqAcked - state->snd_una;
//        state->delivered += newlyAcked;
//
////        state->delivered = lastAckNo->getAckNo() - state->iss;  //In the TcpHeader the method getAckNo returns "ackNo"
//        std::cout << "Delivered is: "<< state->delivered << std::endl;
//
//
//    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
//
////    std::cout << "found: " << found << std::endl;
//
//           if (found != nullptr) {
//
//               simtime_t RTT = simTime() - (found->getFirstSentTime());
//
//               state->m_lastRtt = RTT;
//
////              std::cout << "FirstSentTime: " << found->getFirstSentTime() << std::endl;       // DEBUG
////              std::cout << "RTT_new: " << RTT << std::endl;                                    // DEBUG
////              std::cout << "RTT_original: " << state->rtt << std::endl;                       // DEBUG
////
////          //Find The RTT min
////          if(state->m_minRtt == SIMTIME_ZERO || state->m_minRtt > RTT)
////          {
////              state->m_minRtt = RTT;
////          }
//       }
//
//       CongControl(state, newlyAcked);
//
//       debugBBR();
//
//    // Implement BBR-specific logic for updating pacing rate and congestion window
////    SetPacingRate();
////    updateCongestionWindow();
//
//
//    // Handle bandwidth probing logic
//    if (state->probe_rtt_mode_)
//        HandleProbeRTT(state);
//
//    state->time_lastAck = now;
//    state->first_sent_time =  state->time_last_data_sent = SIMTIME_ZERO;
//    // state->m_delivered = 0;  (?)
//
//
//    // try to send more data
//    sendData(false);
//}


//void TcpBBR::receivedDataAck(uint32 firstSeqAcked)
//{
//
//    // --- 0. Capture old snd_una BEFORE base class updates it ---
//    uint32_t oldSndUna = state->snd_una;
//
//    EV_DETAIL << "oldSndUna: " << oldSndUna << "\n";
//
//    // Call base TCP class to update core state (snd_una, rexmit, etc)
//    TcpBaseAlg::receivedDataAck(firstSeqAcked);
//
//    EV_DETAIL << "firstSeqAcked: " << firstSeqAcked << "\n";
//    EV_DETAIL << "NewSndUna: " << state->snd_una << "\n";
//
//    simtime_t now = simTime();
//
//    // --- 1. Update acked/sacked bytes tracking for BBR rate sampling ---
//    // ackedBytes: how many bytes were newly ACKed in this event
//    uint32_t newlyAcked = firstSeqAcked > oldSndUna ? firstSeqAcked - oldSndUna : 0;
//
//    EV_DETAIL << "newlyAcked: " << newlyAcked << "\n";
//
////    if (newlyAcked == 0)
////        return; // no delivery progress → no BBR update
//
////    state->ackedBytes = newlyAcked;
////    state->m_ackedSacked += newlyAcked + state->sackedBytes;
////
////    // --- 2. Update delivered counters for BBR ---
////    state->sentBytes += newlyAcked;  // sentBytes = m_deliveredBytes
////    CongControl(state, ackedBytes);
////
//
//    state->ackedBytes = newlyAcked;
//    state->m_ackedSacked += newlyAcked + state->sackedBytes;
//    EV_DETAIL << "m_ackedSacked: " << state->m_ackedSacked << "\n";
//    state->m_delivered += newlyAcked;   // THIS is BBR delivered
//
//    // --- 4. Call main BBR control logic ---
////    CongControl(state, newlyAcked);
//
//    debugBBR();
//
//    EV_DETAIL << "Delivered: " << state->m_delivered << "\n";
//
//
//    // --- 3. Track send phase timing for rate sample ---
//    // Only set first_sent_time if not set already (start of sample interval)
//    if (state->first_sent_time == SIMTIME_ZERO)
//        state->first_sent_time = state->time_last_data_sent; // Or simTime() if you set this elsewhere
//
//    // last_sent_time should be updated each time data is sent in sendData(), not here
//
//    simtime_t sendElapsed = (state->first_sent_time != SIMTIME_ZERO && state->time_last_data_sent != SIMTIME_ZERO)
//                                ? (state->time_last_data_sent - state->first_sent_time)
//                                : SIMTIME_ZERO;
//    EV_DETAIL << "sendElapsed: " << sendElapsed << "\n";
//
//    // --- 4. Track ack phase timing for rate sample ---
//    // time_firstAck: set on first ACK in interval
//    if (state->time_firstAck == SIMTIME_ZERO)
//        state->time_firstAck = now;
//
//    simtime_t ackElapsed = (state->time_lastAck != SIMTIME_ZERO)
//                                ? (now - state->time_lastAck)
//                                : sendElapsed; // fallback if first ACK
//    EV_DETAIL << "ackElapsed: " << ackElapsed << "\n";
//
//    // --- 5. Calculate BBR sampling interval and delivery rate ---
//    state->m_interval = std::max(sendElapsed, ackElapsed);
//
//    if (state->m_interval > SIMTIME_ZERO) {
//        state->m_deliveryRate = (double)state->sentBytes / state->m_interval.dbl(); // bytes per second
//        state->sentBytes = 0;
//        EV_DETAIL << "BBR Delivery Rate: " << state->m_deliveryRate << " bytes/sec\n";
//    }
//
//    // --- 6. Save bytes-in-flight prior to ACK (for BBR gain calculations) ---
//    state->m_priorInFlight = state->snd_nxt - state->snd_una;
//
//    // --- 7. Optionally, update last RTT if using transmit info ---
//    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
//    if (found != nullptr) {
//        simtime_t RTT = now - found->getFirstSentTime();
//        state->m_lastRtt = RTT;
//    }
//
//    // --- 8. BBR core logic: pacing rate, congestion window, probe RTT ---
////    updatePacingRate(); //method removed
//    SetPacingRate(state, state->m_pacingGain);
////    updateCongestionWindow();
//    UpdateTargetCwnd(state);
//    conn->emit(cwndSignal, state->snd_cwnd);
//
//    if (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT)  // state->probe_rtt_mode_
//        HandleProbeRTT(state);
//
//    // --- 9. Update last ACK time, reset interval timers ---
//    state->time_lastAck = now;
//    state->first_sent_time = state->time_last_data_sent = SIMTIME_ZERO;
//
//    // Reset delivered in interval if needed:
//    // state->sentBytes = 0; // Uncomment if you want to reset for next interval
//
//    // --- 10. Try to send more data if available ---
//    sendData(false);
//}


void TcpBBR::segmentRetransmitted(uint32 fromseq, uint32 toseq)
{
    TcpBaseAlg::segmentRetransmitted(fromseq, toseq);

    state->regions.set(fromseq, toseq, simTime());
}

void TcpBBR::dataSent(uint32_t fromseq) {
    TcpBaseAlg::dataSent(fromseq);

    // save the time the packet was sent
    // fromseq is the seq number of the 1st sent byte

    simtime_t sendtime = simTime();
    state->regions.clearTo(state->snd_una);
    state->regions.set(fromseq, state->snd_max, sendtime);

}




// Eventual alternative

//void TcpBBR::receivedDataAck(uint32 firstSeqAcked)
//{
//    // 1. Update base TCP state
//    TcpBaseAlg::receivedDataAck(firstSeqAcked);
//
//    // 2. RTT Measurement and minRTT update
//    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
//    if (found != nullptr) {
//        simtime_t currentTime = simTime();
//        simtime_t rttSample = currentTime - (found->getFirstSentTime());
//        if (rttSample > SIMTIME_ZERO && (state->m_minRtt == SIMTIME_ZERO || rttSample < state->m_minRtt))
//            state->m_minRtt = rttSample;
//
//    debugBBR();
//    }
//
//    std::cout << "m_minRtt: " << state->m_minRtt << std::endl;
//
//    // 3. Handle BBR Recovery logic
//    if (state->lossRecovery) {
//        // Exit recovery if fully ACKed up to 'recover'
//        if (seqGE(state->snd_una - 1, state->recover)) {
//            state->lossRecovery = false;
//            state->firstPartialACK = false;
//            state->m_packetConservation = false;    // BBR-specific
//            // Optionally: restore cwnd if you reduced it on entry (BBR doesn't typically deflate)
//            // bbr_restore_cwnd(state);
//            EV_INFO << "BBR: Loss Recovery terminated.\n";
//        } else {
//            // Still in recovery, retransmit first unacked segment (if applicable)
//            EV_INFO << "BBR: Partial ACK during recovery, retransmitting...\n";
//            conn->retransmitOneSegment(false);
//            // Stay in packet conservation mode
//            // Optionally restart retransmit timer on first partial ACK
//            if (!state->firstPartialACK) {
//                state->firstPartialACK = true;
//                restartRexmitTimer();
//            }
//        }
//    }
//
//    // 4. Delivery rate/counters/sample updates for BBR logic
//    // This is where you update: delivered bytes, rate sample interval, app-limited flag, etc.
//    // Example:
//    uint32_t newlyAcked = firstSeqAcked - state->snd_una;
//    if (newlyAcked > 0) {
//        state->ackedBytes = newlyAcked;
//        state->m_ackedSacked += newlyAcked + state->sackedBytes;
//        state->sentBytes += newlyAcked;
////        state->m_delivered += newlyAcked;   // BBR delivered bytes for bandwidth sample, comment if you call it in CongControl();
//    }
//
//    CongControl(state,state->sentBytes);
//    state->sentBytes = 0;
//
//    // 5. BBR state/model updates
//    // -- update round, full pipe, cycle phase, minRTT, bandwidth, etc
////    UpdateModelAndState(state); // covers CheckRound, CheckFullPipe, etc
//
//    // 6. BBR control parameter update
////    UpdateControlParameters(state); // covers pacing, cwnd, etc
//
//    // 7. Send new data (if permitted by new cwnd/pacing)
//    sendData(false);
//}



//**************
//void TcpBBR::receivedDataAck(uint32 firstSeqAcked)
//{
//
//    // === 2. Call base class to update core TCP state ===
//    TcpBaseAlg::receivedDataAck(firstSeqAcked);
//
//    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
//
////    std::cout << "found: " << found << std::endl;
//
//       if (found != nullptr) {
//           simtime_t currentTime = simTime();
//
//           simtime_t RTT = currentTime - (found->getFirstSentTime());
//
////           std::cout << "FirstSentTime: " << found->getFirstSentTime() << std::endl;       // DEBUG
////           std::cout << "RTT_new: " << RTT << std::endl;                                    // DEBUG
////           std::cout << "RTT_original: " << state->rtt << std::endl;                       // DEBUG
//
//           state->m_minRtt = RTT;
//
//           std::cout << "RTT: " << RTT << std::endl;
//           std::cout << "m_minRtt: " << state->m_minRtt << std::endl;
////           UpdateRTprop(state);
//
////           std::cout << "m_minRtt: " << state->m_minRtt << std::endl;
//
//           debugBBR();
//
//
//
////       //Find The RTT min
////       if(state->m_minRtt == SIMTIME_ZERO || state->m_minRtt > RTT)
////       {
////           state->m_minRtt = RTT;
////
////           debugBBR();
////       }
//   }
//
//
//       if (state->lossRecovery) {
//           if (seqGE(state->snd_una - 1, state->recover)) {
//
//           state->lossRecovery = false;
//           state->firstPartialACK = false;
//           EV_INFO << "Loss Recovery terminated.\n";
//
//           CongControl(state, firstSeqAcked);
//
//           }
//           else {
//               EV_INFO << "Fast Recovery - Partial ACK received: retransmitting the first unacknowledged segment\n";
//               conn->retransmitOneSegment(false);
//
//               CongestionStateSet(state);
//
//               // try to send a new segment if permitted by the new value of cwnd
//               sendData(false);
//
//               // reset REXMIT timer for the first partial ACK that arrives during Fast Recovery
//               if (state->lossRecovery) {
//                   if (!state->firstPartialACK) {
//                       state->firstPartialACK = true;
//                       EV_DETAIL << "First partial ACK arrived during recovery, restarting REXMIT timer.\n";
//                       restartRexmitTimer();
//
//                   }
//                }
//              }
//           }
//       else {   // Perform CongestionStateSet if it is the first connection, perform CongControl otherwise
//
//           if (!state->m_isInitialized){
//
//           CongestionStateSet(state);
//
//           }
//           else {
//
//
//           //    // --- 1. Update acked/sacked bytes tracking for BBR rate sampling ---
//           //    // ackedBytes: how many bytes were newly ACKed in this event
//
//               uint32_t newlyAcked = (firstSeqAcked > state->lastAcked) ? (firstSeqAcked - state->lastAcked) : 0;
//               state->lastAcked = firstSeqAcked;
//
////               std::cout << "firstSeqAcked: " << firstSeqAcked << std::endl;       // DEBUG
////               std::cout << "snd_una: " << state->snd_una << std::endl;       // DEBUG
////               std::cout << "newlyAcked: " << newlyAcked << std::endl;       // DEBUG
//
//               EV_DETAIL << "newlyAcked: " << newlyAcked << "\n";
//
//               if (newlyAcked == 0)
//                   return; // No new data ACKed
//
//               state->ackedBytes = newlyAcked;
//               state->m_ackedSacked += newlyAcked + state->sackedBytes;
//               // update delivered bytes counter as needed for bandwidth sampling
//               state->sentBytes += newlyAcked;
//
////               std::cout << "SentBytes in StartUP: " << state->sentBytes << std::endl;       // DEBUG
//
//               CongControl(state, newlyAcked);
//
//               debugBBR();
//
//           }
//
//       }
//
//       // === 10. Try to send more data if available ===
//
////       CongControl();
//       sendData(false);
//
////       state->sentBytes = 0;
//   }

//**************


void TcpBBR::receivedDataAck(uint32 firstSeqAcked)
{

    // === 2. Call base class to update core TCP state ===
    TcpBaseAlg::receivedDataAck(firstSeqAcked);

    // --- 1. Compute newlyAcked bytes ---

    uint32 newlyAcked = firstSeqAcked > state->last_ack_sent
                      ? firstSeqAcked - state->last_ack_sent
                      : 0; // protect against out-of-order
    state->last_ack_sent = firstSeqAcked;

//    std::cout << "firstSeqAcked: " << firstSeqAcked << std::endl;
//    std::cout << "last_ack_sent: " << state->last_ack_sent << std::endl;
//    std::cout << "newlyAcked: " << newlyAcked << std::endl;

    // --- 2. RTT Measurement: ---
    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
    if (found != nullptr) {
        simtime_t rttSample = simTime() - found->getFirstSentTime();
        state->m_interval = rttSample;

        if (state->m_minRtt == SIMTIME_ZERO || rttSample < state->m_minRtt)
            state->m_minRtt = rttSample;
        state->m_lastRtt = rttSample;
    }

    if (state->m_interval > 0 && newlyAcked > 0) {
        double bw_sample = (double)newlyAcked / state->m_interval; // bytes/sec
        state->m_deliveryRate = bw_sample;
        state->m_maxBwFilter.Update(bw_sample, simTime().inUnit(SIMTIME_MS));

//        state->m_deliveryRate = bw_sample;
        state->m_deliveryRate = (double)state->sentBytes / state->m_interval.dbl(); // bytes/sec
    }


          state->ack_cnt++;
          state->m_ackedSacked = state->last_ack_sent + state->sackedBytes;

         CongControl(state, state->sentBytes);

             // Update pacing and target cwnd (if not handled in CongControl)
             SetPacingRate(state, state->m_pacingGain);
             UpdateTargetCwnd(state);
             conn->emit(cwndSignal, state->snd_cwnd);

             if (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT)
                 HandleProbeRTT(state);

             // === 9. Reset ACK timing and intervals for next sample ===
             state->time_lastAck = simTime();
             state->first_sent_time = state->time_last_data_sent = SIMTIME_ZERO;

             state->time_firstAck = SIMTIME_ZERO;



       sendData(false);
       debugBBR();

   }



//---------------------------------------------------------------------------------------------------------------//
//void TcpBBR::receivedDataAck(uint32 firstSeqAcked)
//{
//
//    // === 2. Call base class to update core TCP state ===
//    TcpBaseAlg::receivedDataAck(firstSeqAcked);
//
//
//
//
////    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
////
////    std::cout << "found: " << found << std::endl;
////
////       if (found != nullptr) {
////           simtime_t currentTime = simTime();
////
////           simtime_t RTT = currentTime - (found->getFirstSentTime());
////
//////           std::cout << "FirstSentTime: " << found->getFirstSentTime() << std::endl;       // DEBUG
//////           std::cout << "RTT_new: " << RTT << std::endl;                                    // DEBUG
//////           std::cout << "RTT_original: " << state->rtt << std::endl;                       // DEBUG
////
////
////
////       //Find The RTT min
////       if(state->m_minRtt != SIMTIME_ZERO || state->m_minRtt > RTT)
////       {
////           state->m_minRtt = RTT;
////       }
////
////       std::cout << "m_minRtt: " << state->m_minRtt << std::endl;
////   }
//
//
//    // 2. RTT Measurement and minRTT update
//    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
//    if (found != nullptr) {
//        simtime_t currentTime = simTime();
//        simtime_t rttSample = currentTime - (found->getFirstSentTime());
//        state->m_interval = rttSample;
//        if (rttSample > SIMTIME_ZERO && (state->m_minRtt == SIMTIME_ZERO || rttSample < state->m_minRtt))
//            state->m_minRtt = rttSample;
//
//    debugBBR();
//    }
//
//
////    SetCwnd(state);  // it is called by CongControl
//
////    std::cout << "m_minRtt: " << state->m_minRtt << std::endl;
//
//
//    // === 3. Compute newlyAcked ===
////    uint32_t newlyAcked = (firstSeqAcked > oldSndUna) ? (firstSeqAcked - oldSndUna) : 0;  # with this was always zero
////    if (newlyAcked == 0)
////        return; // No new delivery progress
//    uint32 newlyAcked = firstSeqAcked - state->snd_una; //oldSndUna;  normally new acks received (Nr) should be greater than oldSndUna, and their diff gives how much is newly acknowledged
//    uint32 newlyAcked2;
//    newlyAcked2 = firstSeqAcked;
//
//               std::cout << "firstSeqAcked: " << firstSeqAcked << std::endl;       // DEBUG
//               std::cout << "snd_una: " << state->snd_una << std::endl;                                    // DEBUG
//               std::cout<< "newlyAcked2 Bytes = "<< newlyAcked2 << std::endl;         // DEBUG
//
//
//    simtime_t now = simTime();
//
//
//    // === 4. Update delivered and tracking variables ===
//    state->ackedBytes = newlyAcked2;
//    state->m_ackedSacked += newlyAcked2 + state->sackedBytes;
//    state->sentBytes += newlyAcked2;
////    state->m_delivered += newlyAcked;   // BBR: total bytes delivered so far (cumulative)
//
//    EV_DETAIL << "Delivered: " << state->m_delivered << "\n";
//
//// ---------------------------------
//    // === 5. RTT tracking (update min_rtt and last_rtt) ===
//    // Use timestamp or segment tracking (optional, but improves BBR accuracy)  {Not sure, keep it commented for now}
//
////simtime_t rttSample = SIMTIME_ZERO;
////const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
////if (found != nullptr) {
////EV_DETAIL << "found: " << found << "\n";
////rttSample = now - found->getFirstSentTime();
////state->m_lastRtt = rttSample;
////// Update min RTT
////if (state->m_minRtt == SIMTIME_ZERO || (rttSample > SIMTIME_ZERO && rttSample < state->m_minRtt)) {
////    state->m_minRtt = rttSample;
////    state->m_minRttStamp = now;
////}
////}
//// ----------------------------------
//
//    // === 6. BBR: Delivery rate and interval logic ===
////    if (state->first_sent_time == SIMTIME_ZERO)
////        state->first_sent_time = state->time_last_data_sent;
////
////    simtime_t sendElapsed = (state->first_sent_time != SIMTIME_ZERO && state->time_last_data_sent != SIMTIME_ZERO)
////        ? (state->time_last_data_sent - state->first_sent_time)
////        : SIMTIME_ZERO;
////
////    if (state->time_firstAck == SIMTIME_ZERO)
////        state->time_firstAck = now;
////
////    simtime_t ackElapsed = (state->time_lastAck != SIMTIME_ZERO)
////        ? (now - state->time_lastAck)
////        : sendElapsed;
//
////    state->m_interval = state->m_minRtt;  //state->time_firstAck - state->first_sent_time; // std::max(sendElapsed, ackElapsed);
//    std::cout<< "m_interval = "<< state->m_interval << std::endl;            // DEBUG
//    std::cout<< "OriginalRTT = "<< state->rtt << std::endl;            // DEBUG
//
//    // Calculate delivery rate for this interval
//    if (state->m_interval > SIMTIME_ZERO && newlyAcked2 > 0) {
//        double bw_sample = (double)state->m_ackedSacked / state->m_interval.dbl();
//        state->m_maxBwFilter.Update(bw_sample, simTime().inUnit(SIMTIME_MS));
//        state->m_deliveryRate = bw_sample;
//
//
//        // DEBUG for m_maxBwFilter and DeliveryRate
////        std::cout << "Bw_sample: " << bw_sample << std::endl;
////        std::cout << "DeliveryRate: " << state->m_deliveryRate << std::endl;
////        std::cout << "m_maxBwFilter in ReceiveDataAck (Best): " << state->m_maxBwFilter.GetBest() << std::endl;
//
////        state->m_deliveryRate = (double)state->sentBytes / state->m_interval.dbl(); // bytes/sec
////        state->sentBytes = 0; // Reset for next interval
//
//        EV_INFO << "ACK: newlyAcked=" << newlyAcked
//                << " interval=" << state->m_interval
//                << " bw_sample=" << bw_sample << endl;
//
//    }
//
//    // === 7. Save in-flight before further updates ===
//    state->m_priorInFlight = state->snd_nxt - state->snd_una;
//
//    // === 8. Call BBR control logic ===
//    CongControl(state, firstSeqAcked);
//
//    debugBBR();
//
//    // Update pacing and target cwnd (if not handled in CongControl)
////    SetPacingRate(state, state->m_pacingGain);
////    UpdateTargetCwnd(state);
//    conn->emit(cwndSignal, state->snd_cwnd);
//
//    if (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT)
//
//        HandleProbeRTT(state);
//
//    // === 9. Reset ACK timing and intervals for next sample ===
//    state->time_lastAck = now;
//    state->first_sent_time = state->time_last_data_sent = SIMTIME_ZERO;
//
//    state->time_firstAck = SIMTIME_ZERO;
//
//    // === 10. Try to send more data if available ===
//    sendData(false);
//
//}


//---------------------------------------------------------------------------------------------------------------//



//void TcpBBR::receivedDataAck(uint32 firstSeqAcked)
//{
//
//    // === 2. Call base class to update core TCP state ===
//    TcpBaseAlg::receivedDataAck(firstSeqAcked);
//
//    const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
//
////    std::cout << "found: " << found << std::endl;
//
//       if (found != nullptr) {
//           simtime_t currentTime = simTime();
//
//           simtime_t RTT = currentTime - (found->getFirstSentTime());
//
////           std::cout << "FirstSentTime: " << found->getFirstSentTime() << std::endl;       // DEBUG
////           std::cout << "RTT_new: " << RTT << std::endl;                                    // DEBUG
////           std::cout << "RTT_original: " << state->rtt << std::endl;                       // DEBUG
//
//
//
//       //Find The RTT min
//       if(state->m_minRtt == SIMTIME_ZERO || state->m_minRtt > RTT)
//       {
//           state->m_minRtt = RTT;
//       }
//
////       std::cout << "m_minRtt: " << state->m_minRtt << std::endl;
//   }
//
//
//
////    std::cout<< "oldSndUnaNEW = "<< oldSndUna << std::endl;            // DEBUG
////    std::cout<< "firstSeqAcked = "<< firstSeqAcked << std::endl;    // DEBUG
////    std::cout<< "snd_max = "<< state->snd_max << std::endl;         // DEBUG
//
//
//    // === 3. Compute newlyAcked ===
////    uint32_t newlyAcked = (firstSeqAcked > oldSndUna) ? (firstSeqAcked - oldSndUna) : 0;  # with this was always zero
////    if (newlyAcked == 0)
////        return; // No new delivery progress
//    uint32_t newlyAcked = firstSeqAcked - state->snd_una; //oldSndUna;  normally new acks received (Nr) should be greater than oldSndUna, and their diff gives how much is newly acknowledged
//
//
//
//    simtime_t now = simTime();
////    std::cout<< "newlyAcked Bytes = "<< newlyAcked << std::endl;
//
//    // === 4. Update delivered and tracking variables ===
//    state->ackedBytes = newlyAcked;
//    state->m_delivered += newlyAcked;   // BBR: total bytes delivered so far (cumulative)
//    state->m_ackedSacked += newlyAcked + state->sackedBytes;
//
//    EV_DETAIL << "Delivered: " << state->m_delivered << "\n";
//
//// ---------------------------------
//    // === 5. RTT tracking (update min_rtt and last_rtt) ===
//    // Use timestamp or segment tracking (optional, but improves BBR accuracy)  {Not sure, keep it commented for now}
//
////simtime_t rttSample = SIMTIME_ZERO;
////const TcpSegmentTransmitInfoList::Item *found = state->regions.get(firstSeqAcked);
////if (found != nullptr) {
////EV_DETAIL << "found: " << found << "\n";
////rttSample = now - found->getFirstSentTime();
////state->m_lastRtt = rttSample;
////// Update min RTT
////if (state->m_minRtt == SIMTIME_ZERO || (rttSample > SIMTIME_ZERO && rttSample < state->m_minRtt)) {
////    state->m_minRtt = rttSample;
////    state->m_minRttStamp = now;
////}
////}
//// ----------------------------------
//
//    // === 6. BBR: Delivery rate and interval logic ===
////    if (state->first_sent_time == SIMTIME_ZERO)
////        state->first_sent_time = state->time_last_data_sent;
////
////    simtime_t sendElapsed = (state->first_sent_time != SIMTIME_ZERO && state->time_last_data_sent != SIMTIME_ZERO)
////        ? (state->time_last_data_sent - state->first_sent_time)
////        : SIMTIME_ZERO;
////
////    if (state->time_firstAck == SIMTIME_ZERO)
////        state->time_firstAck = now;
////
////    simtime_t ackElapsed = (state->time_lastAck != SIMTIME_ZERO)
////        ? (now - state->time_lastAck)
////        : sendElapsed;
//
//    state->m_interval = state->time_firstAck - state->first_sent_time; // std::max(sendElapsed, ackElapsed);
////    std::cout<< "m_interval = "<< state->m_interval << std::endl;            // DEBUG
//
//    // Calculate delivery rate for this interval
//    if (state->m_interval > SIMTIME_ZERO && newlyAcked > 0) {
//        double bw_sample = (double)newlyAcked / state->m_interval.dbl();
//        state->m_maxBwFilter.Update(bw_sample, simTime().inUnit(SIMTIME_MS));
//        state->m_deliveryRate = bw_sample;
////        state->m_deliveryRate = (double)state->sentBytes / state->m_interval.dbl(); // bytes/sec
////        state->sentBytes = 0; // Reset for next interval
//
//        EV_INFO << "ACK: newlyAcked=" << newlyAcked
//                << " interval=" << state->m_interval
//                << " bw_sample=" << bw_sample << endl;
//
//    }
//
//    // === 7. Save in-flight before further updates ===
//    state->m_priorInFlight = state->snd_nxt - state->snd_una;
//
//    // === 8. Call BBR control logic ===
//    CongControl(state, newlyAcked);
//
//    debugBBR();
//
//    // Update pacing and target cwnd (if not handled in CongControl)
//    SetPacingRate(state, state->m_pacingGain);
//    UpdateTargetCwnd(state);
//    conn->emit(cwndSignal, state->snd_cwnd);
//
//    if (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT)
//        HandleProbeRTT(state);
//
//    // === 9. Reset ACK timing and intervals for next sample ===
//    state->time_lastAck = now;
//    state->first_sent_time = state->time_last_data_sent = SIMTIME_ZERO;
//
//    state->time_firstAck = SIMTIME_ZERO;
//
//    // === 10. Try to send more data if available ===
//    sendData(false);
//}
//


// #######----------------------#######





//void TcpBBR::rttMeasurementComplete(simtime_t tSent, simtime_t tAcked) {              //  const simtime_t& rtt
//    TcpBaseAlg::rttMeasurementComplete(tSent, tAcked);   // simtime_t tSent, simtime_t tAcked
//
//    // Update BBR-specific variables after RTT measurement
//    simtime_t rttSec = state->srtt;   // .dbl();    //
//  state->m_minRtt = std::min(state->m_minRtt, rttSec);   //min_rtt_
//  state->bdp_ = calculateBDP();
//
//    // Update bandwidth estimation variables
//    if (!state->probe_rtt_mode_) {
//        state->max_bw_ = std::max(state->max_bw_, state->snd_cwnd / rttSec);    //state->snd_cwnd = cwnd_
//        state->full_bw_ = state->max_bw_;
//    }
//}




//void TcpBBR::updatePacingRate() {
//    if (state->probe_rtt_mode_) {
//        // Implement BBR's pm_ackedSackedacing rate calculation logic during RTT probing mode
//        // Example: pacing_rate_ = pacing_gain_ * full_bw_
//        state->pacing_rate_ = state->pacing_gain_ * state->full_bw_;
//    } else {
//        // Implement BBR's pacing rate calculation logic during normal operation
//        // Example: pacing_rate_ = pacing_gain_ * bdp_
//        state->pacing_rate_ = state->pacing_gain_ * state->bdp_;
//    }
//}

//void TcpBBR::updateCongestionWindow() {
//    if (state->probe_rtt_mode_) {
//        // Implement BBR's congestion window adaptation logic during RTT probing mode
//        // Example: cwnd_ = pacing_rate_ * min_rtt_
//        state->snd_cwnd = state->pacing_rate_ * state->min_rtt_;    //cwnd_
//        state->snd_cwnd = std::max(state->snd_cwnd, state->m_minPipeCwnd);   // Enforce that snd_cwnd must not be 0
//    } else {
//        // Implement BBR's congestion window adaptation logic during normal operation
//        // Example: cwnd_ = cwnd_gain_ * bdp_
//        state->snd_cwnd = state->cwnd_gain_ * state->bdp_;     //cwnd_
//        state->snd_cwnd = std::max(state->snd_cwnd, state->m_minPipeCwnd);   // Enforce that snd_cwnd must not be 0
//    }
//}

double TcpBBR::calculateBDP() {                     // this method is not called by anyone so far that is wwhy BDP = 0
    // Implement BBR's BDP estimation logic here
    // Example: bdp_ = pacing_rate_ * min_rtt_
    double max_bw = state->m_maxBwFilter.GetBest();
//    double min_rtt = state->m_minRtt.dbl();
   return  state->bdp_ = (max_bw * state->m_minRtt.dbl());           //min_rtt_
   debugBBR();
//    return state->pacing_rate_ * state->min_rtt_;
}



//void TcpBBR::startProbeRTT() {                       // analog of EnterProbeRTT()
//    // Enter RTT probing mode
//    state->probe_rtt_mode_ = true;
//    state->pacing_rate_prev_ = state->pacing_rate_;
//    state->round_count_ = 0;
//    state->time_last_data_sent  = simTime();  //last_packet_sent_time_
//
//    // Set pacing rate to a small value to send probe packets
//    state->pacing_rate_ = 0.1 * state->full_bw_;
//    state->snd_cwnd  = 2 * state->snd_mss;   //snd_cwnd = cwnd_    ,  snd_mss = MSS
//}


void TcpBBR::EnterProbeRTT()
{
    EV_DETAIL << "EnterProbeRTT " << state << "\n";
//    BBR_LOG_MODE("EnterProbeRTT()");

    SetBbrState(state->BbrMode_t::BBR_PROBE_RTT);
    std::cout<< "Entered BBR_PROBE_RTT at time t = " << simTime() << std::endl;         // DEBUG

    state->m_pacingGain = 1;
    state->m_cWndGain = 1;
    debugBBR();
}



void TcpBBR::SaveCwnd(TcpBBRStateVariables *& state)
{
    EV_DETAIL << "SaveCwnd " << state << "\n";
    if (!state->lossRecovery && state->m_state != TcpBBRStateVariables::BBR_PROBE_RTT)
    {
        state->m_priorCwnd = state->snd_cwnd;
    }
    else
    {
        state->m_priorCwnd = std::max(state->m_priorCwnd, state->snd_cwnd);
    }

    debugBBR();
}



void TcpBBR::RestoreCwnd(TcpBBRStateVariables *& state)
{
    state->snd_cwnd = std::max(state->m_priorCwnd, state->snd_cwnd);

    EV_DETAIL << "RestoreCwnd " << state->snd_cwnd << "\n";

}



//void TcpBBR::handleProbeRTT() {
//    if (simTime() - state->time_last_data_sent >= state->min_rtt_) {                                 // ****
//        // Time to send another probe packet
//        sendProbePacket();
//        state->time_last_data_sent = simTime();                                 // ***
//        state->round_count_++;
//
//        // Check if RTT probing is complete
//        if (state->round_count_ >= 8)
//            updateProbeRTT();
//    }
//}




void TcpBBR::CheckProbeRTT(TcpBBRStateVariables *& state)    // , const TcpRateOps::TcpRateSample& rs
{
    EV_DETAIL << "CheckProbeRTT " << state << "\n";
    if (state->m_state != TcpBBRStateVariables::BBR_PROBE_RTT && state->m_minRttExpired && !state->m_idleRestart)
    {
        debugBBR();
        EnterProbeRTT();
        SaveCwnd(state);
        state->m_probeRttDoneStamp = SimTime(0);
    }

    if (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT)
    {
        HandleProbeRTT(state);
    }

    if (state->m_delivered > 0)
    {
        state->m_idleRestart = false;
    }
}

void TcpBBR::SetSendQuantum(TcpBBRStateVariables *& state)
{
    EV_DETAIL << "SetSendQuantum " << state << "\n";
    state->m_sendQuantum = 1 * state->snd_mss;
}

void TcpBBR::UpdateTargetCwnd(TcpBBRStateVariables *& state)
{
//    EV_DETAIL << "UpdateTargetCwnd " << state << "\n";
    state->m_targetCWnd = InFlight(state, state->m_cWndGain) + AckAggregationCwnd();
    // Enforce minimum window
        state->m_targetCWnd = std::max(state->m_targetCWnd, state->m_minPipeCwnd);
        state->snd_cwnd = state->m_targetCWnd;

    EV_DETAIL << "Updated m_targetCWnd: " << state->m_targetCWnd << "\n";


//        // Grow (or shrink) snd_cwnd as per BBR mode
//        if (state->m_state == TcpBBRStateVariables::BBR_STARTUP)
//            state->snd_cwnd = std::min(state->snd_cwnd + newlyAcked, state->m_targetCWnd);
//        else
//            state->snd_cwnd = state->m_targetCWnd; // (or some other logic based on BBR phase)
//
//        state->snd_cwnd = std::max(state->snd_cwnd, state->m_minPipeCwnd);

}


uint32_t TcpBBR::AckAggregationCwnd()
{
    EV_DETAIL << "AckAggregationCwnd " << state << "\n";
    uint32_t maxAggrBytes = 0; // MaxBW * 0.1 secs
    uint32_t aggrCwndBytes = 0;

    if (state->m_extraAckedGain && state->m_isPipeFilled)
    {
        // maxBw in bytes/sec (check units for your implementation!)
        double maxBw = state->m_maxBwFilter.GetBest();
        // 0.1 seconds worth of max bandwidth:
        maxAggrBytes = static_cast<uint32_t>(maxBw * 0.1);

//        maxAggrBytes = state->m_maxBwFilter.GetBest() / (10 * 8);   // GetBest().GetBitRate()
        aggrCwndBytes = state->m_extraAckedGain * std::max(state->m_extraAcked[0], state->m_extraAcked[1]);
        aggrCwndBytes = std::min(aggrCwndBytes, maxAggrBytes);
    }
    return aggrCwndBytes;
}

void TcpBBR::UpdateAckAggregation(TcpBBRStateVariables *& state)     // , const TcpRateOps::TcpRateSample& rs
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
    expectedAcked = state->m_maxBwFilter.GetBest() * epochProp / 8;   // .GetBitRate()

    if (state->m_ackEpochAcked <= expectedAcked ||
        (state->m_ackEpochAcked + state->m_ackedSacked >= state->m_ackEpochAckedResetThresh))
    {
        state->m_ackEpochAcked = 0;
        state->m_ackEpochTime = simTime();
        expectedAcked = 0;
    }

    state->m_ackEpochAcked = state->m_ackEpochAcked + state->m_ackedSacked;
    extraAck = state->m_ackEpochAcked - expectedAcked;
    extraAck = std::min(extraAck, state->snd_wnd);

    if (extraAck > state->m_extraAcked[state->m_extraAckedIdx])
    {
        state->m_extraAcked[state->m_extraAckedIdx] = extraAck;
    }
}

bool TcpBBR::ModulateCwndForRecovery(TcpBBRStateVariables *& state)     //  , const TcpRateOps::TcpRateSample& rs
{
    EV_DETAIL << "ModulateCwndForRecovery " << state << "\n";
    if (state->m_bytesLoss > 0)
    {
        state->snd_cwnd = std::max((int)state->snd_cwnd - (int)state->m_bytesLoss, (int)state->snd_mss);
    }

    if (state->m_packetConservation)
    {
        state->snd_cwnd = std::max(state->snd_cwnd, state->m_bytesInFlight + state->m_ackedSacked);
        return true;
    }
    return false;
}

void TcpBBR::ModulateCwndForProbeRTT(TcpBBRStateVariables *& state)
{
    EV_DETAIL << "ModulateCwndForProbeRTT " << state << "\n";
    if (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT)
    {
        state->snd_cwnd = std::min(state->snd_cwnd, state->m_minPipeCwnd);
    }
    debugBBR();
}

void TcpBBR::SetCwnd(TcpBBRStateVariables *& state)         // , const TcpRateOps::TcpRateSample& rs
{
    EV_DETAIL << "SetCwnd " << state << "\n";

    // Only update if any new data was acknowledged/SACKed
     if (state->m_ackedSacked == 0)
         return;

    if (state->lossRecovery) // state->m_congState == TcpSocketState::CA_RECOVERY
    {
        if (ModulateCwndForRecovery(state))
        {
            return;
        }
    }

    UpdateTargetCwnd(state);

    if (state->m_isPipeFilled)
    {
        state->snd_cwnd = std::min(state->snd_cwnd + (uint32_t)state->m_ackedSacked, state->m_targetCWnd);
    }
    else if (state->snd_cwnd < state->m_targetCWnd || state->m_delivered < state->m_initialCWnd * state->snd_mss)
    {
        state->snd_cwnd = state->snd_cwnd + state->m_ackedSacked;
    }
    state->snd_cwnd = std::max(state->snd_cwnd, state->m_minPipeCwnd);
    EV_DETAIL << "Congestion window updated. New value:" << state->snd_cwnd << "\n";

    // If we're in PROBE_RTT, enforce special cwnd rules
    ModulateCwndForProbeRTT(state);
}

void TcpBBR::UpdateRound(TcpBBRStateVariables *& state)   // , const TcpRateOps::TcpRateSample& rs
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

void TcpBBR::UpdateBottleneckBandwidth(TcpBBRStateVariables *& state)   //  , const TcpRateOps::TcpRateSample& rs
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
    }
}

void TcpBBR::UpdateModelAndState(TcpBBRStateVariables *& state)  //, const TcpRateOps::TcpRateSample& rs
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

void TcpBBR::UpdateControlParameters(TcpBBRStateVariables *& state)  // , const TcpRateOps::TcpRateSample& rs
{
    EV_DETAIL << "UpdateControlParameters " << state << "\n";
//    std::cout<< "Entered UpdateControlParameters, at time T = " << simTime() << std::endl;         // DEBUG
    SetPacingRate(state, state->m_pacingGain);
    SetSendQuantum(state);
    SetCwnd(state);   // , rs
}




void TcpBBR::HandleProbeRTT(TcpBBRStateVariables *& state)
{
    EV_DETAIL << "HandleProbeRTT " << state << "\n";

    SetAppLimited(state->m_bytesInFlight);      // how much is the m_bytesInFlight here and now? check if should be set

    if (state->m_probeRttDoneStamp == 0 && state->m_bytesInFlight <= state->m_minPipeCwnd)
    {
        state->m_probeRttDoneStamp = simTime() + state->m_probeRttDuration;
        state->m_probeRttRoundDone = false;
        state->m_nextRoundDelivered = state->m_delivered;
    }
    else if (!(state->m_probeRttDoneStamp == 0))
    {
        if (state->m_roundStart)
        {
            state->m_probeRttRoundDone = true;
        }
        if (state->m_probeRttRoundDone && simTime() > state->m_probeRttDoneStamp)
        {
            state->m_minRttStamp = simTime();
            RestoreCwnd(state);

            debugBBR();

            ExitProbeRTT();

        }
    }
    debugBBR();
}


//void TcpBBR::sendProbePacket() {
//    // Implement the logic to send a probe packet
//    // Example: TcpSendQueue::sendProbe()
//}



//void TcpBBR::updateProbeRTT() {
//    // Calculate the actual minimum RTT during probeRTT
//    simtime_t probe_rtt = simTime() - state->time_last_data_sent;     //  last_packet_sent_time_
//    state->min_rtt_ = std::min(state->min_rtt_, probe_rtt.dbl());
//
//    // Restore original pacing rate and exit probeRTT mode
//    state->pacing_rate_ = state->pacing_rate_prev_;
//    state->probe_rtt_mode_ = false;
//}



void TcpBBR::UpdateRTprop(TcpBBRStateVariables *& state)
{
    EV_DETAIL << "UpdateRTprop " << state << "\n";
    state->m_minRttExpired = simTime() > (state->m_minRttStamp + state->m_minRttFilterLen);
    if (state->m_lastRtt >= 0 && (state->m_lastRtt <= state->m_minRtt || state->m_minRttExpired))   // m_lastRtt.Get().IsPositive()
    {
        state->m_minRtt = state->m_lastRtt;
        state->m_minRttStamp = simTime();
    }
}


void TcpBBR::debugBBR()
{
    EV_DETAIL
        << "\n==================== BBR DEBUG ====================\n"
        << "Time:               " << simTime() << "\n"
        << "BBR Mode:           " << state->m_state << " (" << TcpBBRStateVariables::BbrMode_t(state->m_state) << ")\n"
        << "Cycle Index:        " << state->m_cycleIndex << "\n"
        << "snd_cwnd:           " << state->snd_cwnd << "\n"
        << "ssthresh:           " << state->ssthresh << "\n"
        << "InFlight:           " << state->m_bytesInFlight << "\n"
        << "Max BW:             " << state->m_maxBwFilter.GetBest() << "\n" // (" << (isBitsPerSec ? "bits/s" : "bytes/s") << ")\n"
//        << "Pacing Rate OLD:        " << state->pacing_rate_ << " bytes/s\n"
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
//        << "Round Count:        " << state->round_count_ << "\n"
        << "ProbeRTT Mode?:     " << (state->m_state == TcpBBRStateVariables::BBR_PROBE_RTT ? "YES" : "NO") << "\n"
//        << "Extra Acked Interval Start: " << state->extra_acked_interval_start_ << "\n"
//        << "Extra Acked Delivered:      " << state->extra_acked_delivered_ << "\n"
        << "Extra Acked Gain:           " << state->m_extraAckedGain << "\n"
        << "Is Pipe Filled:             " << (state->m_isPipeFilled ? "YES" : "NO") << "\n"
        << "Send Quantum:               " << state->m_sendQuantum << "\n"
        << "===============================================\n"
        << endl;
}




//void TcpBBR::debugBBR()
//{
//    EV_DETAIL << "====================BBR========================"<< endl
//       <<"Time     " << simTime() << endl
//       <<"Cwnd now " << state->snd_cwnd <<endl
//       <<"ssthresh " << state->ssthresh <<endl
//       <<"Min RTT  " << state->min_rtt_ << endl
//       <<"Pacing_Rate   " << state->pacing_rate_<< endl
//       <<"Round_Count" << state->round_count_ <<endl
//       <<"BDP"          << state->bdp_ <<endl
//       <<"extra_acked_interval_start    " << state->extra_acked_interval_start_ <<endl
//       <<"extra_acked_delivered    " << state->extra_acked_delivered_ << endl
//       <<"min_rtt_stamp " << state->min_rtt_stamp_ << endl
//       << "============================================"
//       << endl;
//}



