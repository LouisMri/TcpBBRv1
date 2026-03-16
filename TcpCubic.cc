/*
 * TcpCubic.cc
 *
 *  Created on: Sep 28, 2023
 *      Author: martino
 */



#include <algorithm>    // min,max

#include "inet/common/INETMath.h"
#include "inet/transportlayer/tcp/Tcp.h"
#include "inet/transportlayer/tcp/flavours/TcpNewReno.h"
#include "inet/transportlayer/tcp/flavours/TcpCubic.h"
#include <cmath>
#include <cstring>  // for std::memset
#include <cstddef>  // for std::offsetof
#include <cstdint>  // for uint32_t_t and uint64_t

#define ACK_RATIO_SHIFT  4   // Defined in bic

#define BICTCP_BETA_SCALE  1024	/* Scale factor beta calculation
					 * max_cwnd = snd_cwnd * beta
					 */
#define	BICTCP_HZ  10	/* BIC HZ 2^10 = 1024 */

#define HZ  1000   // 1/1000    /* num of ticks (or clock interrupts)per second */

#define SIMTIME_PRECISION 1u  // set simulation time resolution to microseconds


/* Two methods of hybrid slow start */
#define HYSTART_ACK_TRAIN	0x1
#define HYSTART_DELAY		0x2

/* Number of delay samples for detecting the increase of delay */
#define HYSTART_MIN_SAMPLES	8
#define HYSTART_DELAY_MIN	(4000U)	/* 4 ms */
#define HYSTART_DELAY_MAX	(16000U)	/* 16 ms */
#define HYSTART_DELAY_THRESH(x)	inet::math::clamp(x, HYSTART_DELAY_MIN, HYSTART_DELAY_MAX)

//    static int fast_convergence = 1;

static int BETA = 717;  /* = 717/1024 (BICTCP_BETA_SCALE) */
static int initial_ssthresh;
static int bic_scale = 41;

//    static int tcp_friendliness = 1;

//    static int hystart = 1;
//    static int hystart_detect  = HYSTART_ACK_TRAIN | HYSTART_DELAY;
//    static int hystart_low_window = 16;
//    static int hystart_ack_delta_us = 2000;


using namespace inet;
using namespace tcp;


Register_Class(TcpCubic);

TcpCubicStateVariables::TcpCubicStateVariables() {
    
//    The fast convergence is designed for network environments with
//    multiple CUBIC flows.  In network environments with only a single
//    CUBIC flow and without any other traffic, the fast convergence SHOULD
//    be disabled.


    ssthresh;
      fast_convergence;   // true = 1
      tcp_friendliness;
      beta = 0.2; //0.7;
//      hystart; //true;    false                  // if this is "true" the slow start will get very-slow TO BE adjusted



    hystartLowWindow = 16;    //segments
//    hystartDetect(HybridSSDetectionMode::BOTH),
    hystartMinSamples = 8;
    hystartAckDelta = 0.002;   // 2ms
    hystartDelayMin= 0.004;    // 4ms
    hystartDelayMax = 1;       // 1000ms
    cubicDelta = 10; //ms
//    cntClamp = 20;
      c = 0.4;

        ssthresh; //0xFFFFFFFF; // 0xffffffff; /* 15; was UINT32_MAX; */   //4294967295
        cnt = 0;                /* increase cwnd by 1 after ACKs */
        last_max_cwnd = 0;      /* last maximum snd_cwnd */
        last_cwnd = 0;          /* the last snd_cwnd */
        last_time = 0;          /* time when updated last_cwnd */
        bic_origin_point = 0;   /* origin point of bic function */
        bic_K = 0.0;              /* time to origin point from the beginning of the current epoch */
        delay_min;          /* min delay (usec) */
        epoch_start = 0;        /* beginning of an epoch */
        ack_cnt = 0;            /* number of acks */
        tcp_cwnd = 0;           /* estimated tcp cwnd  = snd_cwnd*/
        cWndCnt = 0;

        sample_cnt = 0;         /* number of samples to decide curr_rtt */
        last_ack = SIMTIME_ZERO;           /* last time when the ACK spacing is close */
        curr_rtt = SIMTIME_ZERO;           /* the minimum rtt of current round */
        round_start = SIMTIME_ZERO;
        endSeq = 0;

        found = false;              /* the exit point is found? */

        end_seq = 0;            /* end_seq of the round */

        sampleCnt = 0;
        minRTT = SIMTIME_ZERO;                 // SIMTIME_ZERO        ** SIMTIME_MAX **
        K = SIMTIME_ZERO;
        c_t = SIMTIME_ZERO;

        segmentsAcked =  0;
        /* Precompute a bunch of the scaling factors that are used per-packet
             * based on SRTT of 100ms
             */

           beta_scale = 8*(BICTCP_BETA_SCALE+ BETA) / 3
                / (BICTCP_BETA_SCALE - BETA);                    // if  defined in the state variable need to be called as state->beta if defined as statinc int, only call it like beta..

           cube_rtt_scale = (bic_scale * 10);  /* 1024*c/rtt */

            /* calculate the "K" for (wmax-cwnd) = c/rtt * K^3
             *  so K = cubic_root( (wmax-cwnd)*rtt/c )
             * the unit of K is bictcp_HZ=2^10, not HZ
             *
             *  c = bic_scale >> 10
             *  rtt = 100ms
             *
             * the following code has been designed and tested for
             * cwnd < 1 million packets
             * RTT < 100 seconds
             * HZ < 1,000,00  (corresponding to 10 nano-second)
             */

            /* 1/c * 2^2*bictcp_HZ * srtt */
            // Assuming cube_factor is a double or another numeric type
            cube_factor = pow(2.0, 10 + 3 * BICTCP_HZ);  // 2^40


            /* divide by bic_scale and by constant Srtt (100ms) */
//            do_div = inet::math::div(cube_factor, bic_scale * 10);

}

TcpCubicStateVariables::~TcpCubicStateVariables() {
}

simsignal_t TcpCubic::WmaxSignal = cComponent::registerSignal("Wmax"); // will record the WmaX

void TcpCubic::initialize()
{
    TcpBaseAlg::initialize();
    cubic_reset();
    state->ssthresh = 65535;  //65535;
    state->fast_convergence = true;
    state->tcp_friendliness = true;

    if(state->hystart){
    state->hystart_Detect = state->HybridSSDetectionMode::BOTH;
    }

    // Read the hyStart parameter from the NED file


//    state->found = false;

//    if (state->hystart == true){
//
////        state->hystart_Detect = tcpMain->par("HyStartDetect");
//
//        state->hystart_Detect == state->HybridSSDetectionMode::BOTH;
//
//    }

//    // Register the HyStartDetect attribute
//        cModule *mod = getParentModule();
//        if (mod) {
//            mod->par("HyStartDetect").setEnumValue(HybridSSDetectionMode::BOTH);
//            mod->par("HyStartDetect").setEnumAccessor([this](const cValue& v) {
//                this->m_hystartDetect = static_cast<HybridSSDetectionMode>(v.intValue());
//            });
//            mod->par("HyStartDetect").setEnumChecker(HybridSSDetectionMode::PACKET_TRAIN, "PACKET_TRAIN");
//            mod->par("HyStartDetect").setEnumChecker(HybridSSDetectionMode::DELAY, "DELAY");
//            mod->par("HyStartDetect").setEnumChecker(HybridSSDetectionMode::BOTH, "BOTH");
//
//            }
}



std::string TcpCubicStateVariables::str() const {
    std::stringstream out;
    out << TcpBaseAlgStateVariables::str();

    return out.str();
}

std::string TcpCubicStateVariables::detailedInfo() const {
    std::stringstream out;
    out << TcpBaseAlgStateVariables::detailedInfo();
    out << " ssthresh=" << ssthresh << "\n";
    return out.str();
}

void TcpCubic::setSendQueueLimit(uint32 newLimit){
    // The initial value of ssthresh SHOULD be set arbitrarily high (e.g.,
    // to the size of the largest possible advertised window) -> defined by sendQueueLimit
    state->sendQueueLimit = newLimit;
    state->ssthresh = state->sendQueueLimit;
}


/* Ctor */
TcpCubic::TcpCubic() :
        TcpBaseAlg(), state((TcpCubicStateVariables *&) TcpAlgorithm::state) {
}


double TcpCubic::do_div(double cube_factor, int bic_scale){

    double d = inet::math::div(cube_factor, bic_scale * 10);

    return d;
}


//Record W_max, restart the time at which a loss event occurr and set the ssthresh, recalculateSlowStartThreshold need to be called at each loss event

void TcpCubic::recalculateSlowStartThreshold()       //uint32_t
{
    EV_DEBUG << "recalculateSlowStartThreshold(), ssthresh=" << state->ssthresh
            << "\n";

    state->epoch_start = SIMTIME_ZERO;    /* end of epoch */

//    uint32 flight_size = std::min(state->snd_cwnd, state->snd_wnd);
    uint32 seg_cwnd = state->snd_cwnd / state->snd_mss;     // before state->snd_cwnd = flight_size

    /* Wmax and fast convergence */                            // Before was    "seg_cwnd" = state->snd_cwnd
        if (state->snd_cwnd < state->last_max_cwnd && state->fast_convergence ){   // && state->fast_convergence
            state->last_max_cwnd = (seg_cwnd * (1 + (state->beta))/2) * state->snd_mss  ;   //(BICTCP_BETA_SCALE + BETA)) / (2 * BICTCP_BETA_SCALE);


       //     state->last_max_cwnd = (state->snd_cwnd  * (1 - (state->beta)/2));
        }
//        if (ssthreshVector) {
//            ssthreshVector->record(state->ssthresh);
//        }

        else{
            state->last_max_cwnd = seg_cwnd * state->snd_mss;


//            state->last_max_cwnd = state->snd_cwnd * (1 - state->beta);
        }



//            state->ssthresh = std::max(static_cast<uint32_t>(state->snd_cwnd * state->) / BICTCP_BETA_SCALE, 2U);
//      state->ssthresh = state->snd_cwnd * (1 - state->beta);

//        return std::max((state->snd_cwnd * BETA) / BICTCP_BETA_SCALE, uint32_t(2));

//            return std::max((state->snd_cwnd * BETA) / BICTCP_BETA_SCALE, 2 * state->snd_mss);

//        state->ssthresh = std::max((state->snd_cwnd * BETA) / BICTCP_BETA_SCALE, 2 * state->snd_mss);

        state->ssthresh = std::max(static_cast<uint32_t>(seg_cwnd * state->beta), uint32(2)) * state->snd_mss;   //  (state->seg_cwnd * state->beta), uint32(2)) * state->snd_mss;

        conn->emit(cwndSignal,state->snd_cwnd);
        conn->emit(WmaxSignal,state->last_max_cwnd);
        conn->emit(ssthreshSignal, state->ssthresh);

}




void TcpCubic::processRexmitTimer(TcpEventCode& event)
{
    TcpBaseAlg::processRexmitTimer(event);

    if (event == TCP_E_ABORT)
        return;


    state->recover = (state->snd_max - 1);
    EV_INFO << "recover=" << state->recover << "\n";
    state->lossRecovery = false;
    state->firstPartialACK = false;
    EV_INFO << "Loss Recovery terminated.\n";

//    state->epoch_start = simTime();
//    uint32_t flight_size = std::min(state->snd_cwnd, state->snd_wnd);      //Added
//    state->ssthresh = std::max(flight_size / 2, 2 * state->snd_mss);

    // begin Slow Start (RFC 2581)
    recalculateSlowStartThreshold();
    cubic_reset();

    if(state->hystart){
        HystartReset();
    }

    state->snd_cwnd = state->snd_mss;
//     state->snd_cwnd = state->snd_cwnd * state->beta;


    conn->emit(cwndSignal, state->snd_cwnd);
    conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
    conn->emit(ssthreshSignal, state->ssthresh);                   // added

    EV_INFO << "Begin Slow Start: resetting cwnd to " << state->snd_cwnd
            << ", ssthresh=" << state->ssthresh << "\n";
    state->afterRto = true;
   conn->retransmitOneSegment(true);
}



void TcpCubic::cubic_reset()
{
    state->last_max_cwnd = 0;
    state->epoch_start = SIMTIME_ZERO;
    state->bic_origin_point = 0;
    state->minRTT = SIMTIME_ZERO;
    state->tcp_cwnd = 0;
    state->ack_cnt = 0;
    state->K = 0;

    if(state->hystart){
    state->found = false;
    }
}


void TcpCubic::HystartReset()
{

   state->round_start = state->last_ack = simTime();
   state->endSeq = state->snd_max;                              // ** state->end_seqno ** is the end sequence number of last received out-of-order segment
   state->curr_rtt = SIMTIME_ZERO;
   state->sampleCnt = 0;
}


void TcpCubic::HystartUpdate(TcpCubicStateVariables *& state, const  simtime_t delay)
{

  if (!(state->found))
    {
      simtime_t now = simTime();

      std::cout<< "**DEBUG** time_lastAck in HystartUpdate : "<< state->time_lastAck << std::endl;                      //DEBUG

      /* first detection parameter - ack-train detection */
      if ((now - state->time_lastAck) <= state->hystartAckDelta)
        {
          state->time_lastAck = now;

          if ((now - state->round_start) > state->minRTT)
            {

              if (state->hystart_Detect == state->HybridSSDetectionMode::PACKET_TRAIN ||
                      state->hystart_Detect == state->HybridSSDetectionMode::BOTH)
              {
                  state->found = true;
              }
            }
          }

      /* obtain the minimum delay of more than sampling packets */
      if (state->sample_cnt < state->hystartMinSamples)
        {
          if (state->curr_rtt == SIMTIME_ZERO || state->curr_rtt > delay)
            {
              state->curr_rtt = delay;
            }

          state->sample_cnt++;
        }
      else if (state->curr_rtt > state->minRTT + HystartDelayThresh(state->minRTT))
        {
              state->found = true;
            }
      /*
       * Either one of two conditions are met,
       * we exit from slow start immediately.
       */
      if (state->found)
        {
          EV_INFO << "Exit from Slow Start, immediately :-)"<< "\n";

          state->ssthresh = state->snd_cwnd;

          conn->emit(ssthreshSignal, state->ssthresh);

          conn->emit(WmaxSignal,state->last_max_cwnd);                   // added


        }
    }
}



simtime_t TcpCubic::HystartDelayThresh(const simtime_t t)
{

    simtime_t ret = t;

    if (t > state->hystartDelayMax)
    {
        ret = state->hystartDelayMax;
    }
    else if (t < state->hystartDelayMin)
    {
        ret = state->hystartDelayMin;
    }

    return ret;
}



void TcpCubic::IncreaseWindow()     // (TcpCubicStateVariables *& state, uint32_t segmentsAcked)
{
//    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    if (state->snd_cwnd <= state->ssthresh)        // <
    {
        if (state->hystart && state->last_ack_sent > state->endSeq)
        {
            HystartReset();

            std::cout << "Entered Hystart, Time: " << simTime() << std::endl;                     // DEBUG
        }
//

         state->snd_cwnd += state->snd_mss;


        // In Linux, the QUICKACK socket option enables the receiver to send
        // immediate acks initially (during slow start) and then transition
        // to delayed acks.  ns-3 does not implement QUICKACK, and if ack
        // counting instead of byte counting is used during slow start window
        // growth, when TcpSocket::DelAckCount==2, then the slow start will
        // not reach as large of an initial window as in Linux.  Therefore,
        // we can approximate the effect of QUICKACK by making this slow
        // start phase perform Appropriate Byte Counting (RFC 3465)

//        state->snd_cwnd += segmentsAcked * state->snd_mss;
//        segmentsAcked = 0;


        conn->emit(cwndSignal, state->snd_cwnd);

        conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
        conn->emit(ssthreshSignal, state->ssthresh);                   // added

        EV_INFO << "In SlowStart, updated to cwnd " << state->snd_cwnd << " ssthresh "
                                                     << state->ssthresh;

//        std::cout<< "**DEBUG** CWND in SlowStart: "<< state->snd_cwnd << std::endl;                      // DEBUG
    }

//    if (state->snd_cwnd >= state->ssthresh && segmentsAcked > 0){
   else {


//       state->last_max_cwnd = state->snd_cwnd;    // initially uncommented


        bictcp_update();      // ()state, segmentsAcked   //bictcp_update                //eventually this variable "cnt" can be declared in each methods is being used and not as a state variable as in ns-3
//        std::cout<< "**DEBUG** CWND in CongAvoidance: "<< state->snd_cwnd << std::endl;                       // DEBUG


//        simtime_t t = simTime() + state->minRTT - state->epoch_start ;

//        uint32_t_t W_cubic_t = W_cubic(t);
//        uint32_t_t W_cubic_t_rtt = W_cubic(t + state->epoch_start);

//           if (state->snd_cwnd < state->last_max_cwnd) {
//               // Concave region
//               state->snd_cwnd  = W_cubic_t_rtt;
//
//           } else if (state->snd_cwnd > state->last_max_cwnd) {
//
//               // Convex region
//               state->snd_cwnd  = W_cubic_t_rtt;
//           }

        /* According to RFC 6356 even once the new cwnd is
         * calculated you must compare this to the number of ACKs received since
         * the last cwnd update. If not enough ACKs have been received then cwnd
         * cannot be updated.
         */
        if (state->cWndCnt > state->cnt)  // >=
        {
            state->snd_cwnd += state->snd_mss;   //Probably modify this as a cubic grows

            conn->emit(cwndSignal, state->snd_cwnd);

            conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
            conn->emit(ssthreshSignal, state->ssthresh);                   // added

            state->cWndCnt = 0;
            EV_INFO << "In CongAvoid, updated to cwnd " << state->snd_cwnd;
        }
        else
        {
           state->cWndCnt += 1;

            EV_INFO << "Not enough segments have been ACKed to increment cwnd."
                        "Until now "
                        << state->cWndCnt << " cnd " << state->cnt;
        }

        debugCubic();
    }
}


void TcpCubic::receivedDataAck(uint32_t firstSeqAcked)
{
    TcpBaseAlg::receivedDataAck(firstSeqAcked);

    const TcpSegmentTransmitInfoList::Item *found2 = state->regions.get(firstSeqAcked);

//    std::cout << "found2: " << found2 << std::endl;
       if (found2 != nullptr) {
           simtime_t currentTime = simTime();

           simtime_t RTT = currentTime - (found2->getFirstSentTime());


           std::cout << "FirstSentTime: " << found2->getFirstSentTime() << std::endl;       // DEBUG
           std::cout << "RTT_new: " << RTT << std::endl;                                    // DEBUG
           std::cout << "RTT_original: " << state->rtt << std::endl;                       // DEBUG

       //Find The RTT min
       if(state->minRTT == SIMTIME_ZERO || state->minRTT > RTT)
       {
           state->minRTT = RTT;
       }

       /* hystart triggers when cwnd is larger than some threshold */
       if (state->hystart && state->snd_cwnd <= state->ssthresh && state->snd_cwnd >= state->hystartLowWindow * state->snd_mss)
       {
               HystartUpdate(state, RTT);   //RTT
       }

    }
//       uint32 newlyAcked = firstSeqAcked - state->snd_una;
//          std::cout << "firstSeqAcked: " << firstSeqAcked << std::endl;       // DEBUG
//          std::cout << "snd_una: " << state->snd_una << std::endl;                                    // DEBUG
//          std::cout<< "newlyAcked Bytes = "<< newlyAcked << std::endl;


    // RFC 3782, page 5:
    // "5) When an ACK arrives that acknowledges new data, this ACK could be
    // the acknowledgment elicited by the retransmission from step 2, or
    // elicited by a later retransmission.
    //
    // Full acknowledgements:
    // If this ACK acknowledges all of the data up to and including
    // "recover", then the ACK acknowledges all the intermediate
    // segments sent between the original transmission of the lost
    // segment and the receipt of the third duplicate ACK.  Set cwnd to
    // either (1) min (ssthresh, FlightSize + SMSS) or (2) ssthresh,
    // where ssthresh is the value set in ssegmentsAckedtep 1; this is termed
    // "deflating" the window.  (We note that "FlightSize" in step 1
    // referred to the amount of data outstanding in step 1, when Fast
    // Recovery was entered, while "FlightSize" in step 5 refers to the
    // amount of data outstanding in step 5, when Fast Recovery is
    // exited.)  If the second option is selected, the implementation is
    // encouraged to take measures to avoid a possible burst of data, in
    // case the amount of data outstanding in the network is much less
    // than the new congestion window allows.  A simple mechanism is to
    // limit the number of data packets that can be sent in response to
    // a single acknowledgement; this is known as "maxburst_" in the NS
    // simulator.  Exit the Fast Recovery procedure."

    if (state->lossRecovery) {
        if (seqGE(state->snd_una - 1, state->recover)) {


            /////////////////*********  Added for sack support from TcpReno  *********/////////////////////
//            if (state->sack_enabled){
//                // (A) An incoming cumulative ACK for a sequence number greater than
//                // RecoveryPoint signals the end of loss recovery and the loss
//                // recovery phase MUST be terminated.  Any information contained in
//                // the scoreboard for sequence numbers greater than the new value of
//                // HighACK SHOULD NOT be cleared when leaving the loss recovery
//                // phase."
//                if (seqGE(state->snd_una, state->recoveryPoint)) {
//                    EV_INFO << "Loss Recovery terminated.\n";
//                    state->lossRecovery = false;
//                }
//            }
            /////////////////*********  Added for sack support from TcpReno  *********/////////////////////





            // Exit Fast Recovery: deflating cwnd
            //
            // option (1): set cwnd to min (ssthresh, FlightSize + SMSS)          //here i should call the CUBIC cwnd form, cause it exit fast recovery and the ack of a segment is fully received

//            state->segmentsAcked = firstSeqAcked;

//            uint32_t flight_size = state->snd_max - state->snd_una;                           **

//            state->ack_cnt = state->snd_una - state->last_ack_sent;

//            recalculateSlowStartThreshold();              //             **

            // after Fast Recovery cwnd has to grow has a CUBIC trend


//            state->snd_cwnd = state->snd_cwnd * state->beta;                   //                       // change compare to gitlab



//            IncreaseWindow(state, flight_size);  // flight_size  state->ack_cnt          **     --

//            state->snd_cwnd = std::min(state->ssthresh, flight_size + state->snd_mss);      // NewReno

            state->snd_cwnd = state->ssthresh;             // ++

//            std::cout << "Time: " << simTime() << ", CWND when Full Ack rcvd: "<< state->snd_cwnd << std::endl;                  // DEBUG
//            std::cout << "BLABLABLA ssthresh is: "<< state->ssthresh << std::endl;

            EV_INFO << "Fast Recovery - Full ACK received: Exit Fast Recovery, setting cwnd to " << state->snd_cwnd << "\n";

            // option (2): set cwnd to ssthresh
            // state->snd_cwnd = state->ssthresh;
            // tcpEV << "Fast Recovery - Full ACK received: Exit Fast Recovery, setting cwnd to ssthresh=" << state->ssthresh << "\n";
            // TODO - If the second option (2) is selected, take measures to avoid a possible burst of data (maxburst)!
            conn->emit(cwndSignal, state->snd_cwnd);

            conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
            conn->emit(ssthreshSignal, state->ssthresh);                   // added

            state->lossRecovery = false;
            state->firstPartialACK = false;
            EV_INFO << "Loss Recovery terminated.\n";
        }
        else {

            // RFC 3782, page 5:
            // "Partial acknowledgements:
            // If this ACK does *not* acknowledge all of the data up to and
            // including "recover", then this is a partial ACK.  In this case,
            // retransmit the first unacknowledged segment.  Deflate the
            // congestion window by the amount of new data acknowledged by the
            // cumulative acknowledgement field.  If the partial ACK
            // acknowledges at least one SMSS of new data, then add back SMSS
            // bytes to the congestion window.  As in Step 3, this artificially
            // inflates the congestion window in order to reflect the additional
            // segment that has left the network.  Send a new segment if
            // permitted by the new value of cwnd.  This "partial window
            // deflation" attempts to ensure that, when Fast Recovery eventually
            // ends, approximately ssthresh amount of data will be outstanding
            // in the network.  Do not exit the Fast Recovery procedure (i.e.,
            // if any duplicate ACKs subsequently arrive, execute Steps 3 and 4
            // above).
            //
            // For the first partial ACK that arrives during Fast Recovery, also
            // reset the retransmit timer.  Timer management is discussed in
            // more detail in Section 4."



            /////////////////*********  Added for sack support from TcpReno  *********/////////////////////

//            // update of scoreboard (B.1) has already be done in readHeaderOptions()
//            conn->setPipe();
//
//            // RFC 3517, page 7: "(C) If cwnd - pipe >= 1 SMSS the sender SHOULD transmit one or more
//            // segments as follows:"
//            if (((int)state->snd_cwnd - (int)state->pipe) >= (int)state->snd_mss) // Note: Typecast needed to avoid prohibited transmissions
//                conn->sendDataDuringLossRecoveryPhase(state->snd_cwnd);

            /////////////////*********  Added for sack support from TcpReno  *********/////////////////////





   //martino (from RFC 8312)  with these steps (need to be called at each loss event): restart epoch_start=0, record W_max, rcord epoch_start (new at loss event), deflate cwnd  with beta
//            recalculateSlowStartThreshold(state);         ** --
//            state->epoch_start = simTime();               ** --

//            state->snd_cwnd = state->snd_debugCubiccwnd * state->beta;

            EV_INFO << "Fast Recovery - Partial ACK received: retransmitting the first unacknowledged segment\n";
            // retransmit first unacknowledged segment
            conn->retransmitOneSegment(false);

            // deflate cwnd by amount of new data acknowledged by cumulative acknowledgement field

            state->snd_cwnd -= state->snd_una - firstSeqAcked;       //**

            conn->emit(cwndSignal, state->snd_cwnd);

            conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
            conn->emit(ssthreshSignal, state->ssthresh);                   // added

            EV_INFO << "Fast Recovery: deflating cwnd by amount of new data acknowledged, new cwnd=" << state->snd_cwnd << "\n";

            // if the partial ACK acknowledges at least one SMSS of new data, then add back SMSS bytes to the cwnd
            if (state->snd_una - firstSeqAcked >= state->snd_mss) {

                state->snd_cwnd += state->snd_mss;


                conn->emit(cwndSignal, state->snd_cwnd);

                conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
                conn->emit(ssthreshSignal, state->ssthresh);                   // added

                EV_DETAIL << "Fast Recovery: inflating cwnd by SMSS, new cwnd=" << state->snd_cwnd << "\n";
            }

            // try to send a new segment if permitted by the new value of cwnd
            sendData(false);

            // reset REXMIT timer for the first partial ACK that arrives during Fast Recovery
            if (state->lossRecovery) {
                if (!state->firstPartialACK) {
                    state->firstPartialACK = true;
                    EV_DETAIL << "First partial ACK arrived during recovery, restarting REXMIT timer.\n";
                    restartRexmitTimer();

                }
            }
        }
    }
    else {
                // Perform slow start and congestion avoidance.

        IncreaseWindow();   //state, firstSeqAcked

//        //
//        if (state->snd_cwnd < state->ssthresh) {
//            EV_DETAIL << "cwnd <= ssthresh: Slow Start: increasing cwnd by SMSS bytes to ";
//
//            // perform Slow Start. RFC 2581: "During slow start, a TCP increments cwnd
//            // by at most SMSS bytes for each ACK received that acknowledges new data."
//
//            state->t1 = simTime();
//
//            state->snd_cwnd += state->snd_mss;
//
//            // Note: we could increase cwnd based on the number of bytes being
//            // acknowledged by each arriving ACK, rather than by the number of ACKs
//            // that arrive. This is called "Appropriate Byte Counting" (ABC) and is
//            // described in RFC 3465. This RFC is experimental and probably not
//            // implemented in real-life TCPs, hence it's commented out. Also, the ABC
//            // RFC would require other modifications as well in addition to the
//            // two lines below.
//            //
//            // int bytesAcked = state->snd_una - firstSeqAcked;
//            // state->snd_cwnd += bytesAcked * state->snd_mss;
//
//            conn->emit(cwndSignal, state->snd_cwnd);
//
//            EV_DETAIL << "cwnd=" << state->snd_cwnd << "\n";
//
//
//
//        }
//        else {
//            // perform Congestion Avoidance (RFC 2581)
//
//           /*  CUBIC: After it enters into congestion
//               avoidance from fast recovery, it starts to increase the window using
//               the concave profile of the cubic function. The cubic function is set
//               to have its plateau at W_max so the concave growth continues until
//               the window size becomes W_max.  */
//
//            //martino   (could be possible to call IncreaseWindow(state, state->ack_cnt); here, instead of the following lines?)
//
//            if(state->afterRto){
//
//             simtime_t t = simTime() - state->t1;
//              double t_seconds = t.dbl();
//              state->snd_cwnd += state->c * pow(t_seconds, 3) + state->last_max_cwnd;
//            }
////
//            else{
//            state->ack_cnt = state->snd_una - state->last_ack_sent;
//            IncreaseWindow(state, firstSeqAcked);   //    last_ack_sent    state->ack_cnt   **
////
//
//            std::cout<< "Time: " << simTime() << ",CWND in CA: "<< state->snd_cwnd << std::endl;
//
//            conn->emit(cwndSignal, state->snd_cwnd);
//
//        }

            //
            // Note: some implementations use extra additive constant mss / 8 here
            // which is known to be incorrect (RFC 2581 p5)
            //
            // Note 2: RFC 3465 (experimental) "Appropriate Byte Counting" (ABC)
            // would require maintaining a bytes_acked variable here which we don't do
            //

//            EV_DETAIL << "cwnd > ssthresh: Congestion Avoidance: increasing cwnd linearly, to " << state->snd_cwnd << "\n";
//        }

//         RFC 3782, page 13:
//         "When not in Fast Recovery, the value of the state variable "recover"
//         should be pulled along with the value of the state variable for
//         acknowledgments (typically, "snd_una") so that, when large amounts of
//         data have been sent and acked, the sequence space does not wrap and
//         falsely indicate that Fast Recovery should not be entered (Section 3,
//         step 1, last paragraph)."
        state->recover = (state->snd_una - 2);
    }

    state->regions.clearTo(state->snd_una);

    sendData(false);

}





void TcpCubic::receivedDuplicateAck()
{
    TcpBaseAlg::receivedDuplicateAck();

    if (state->dupacks == DUPTHRESH) {    // DUPTHRESH = 3




/////////////////*********  Added for sack support from TcpReno  *********/////////////////////

//        if (state->sack_enabled) {
//            // RFC 3517, page 6: "When a TCP sender receives the duplicate ACK corresponding to
//            // DupThresh ACKs, the scoreboard MUST be updated with the new SACK
//            // information (via Update ()).  If no previous loss event has occurred
//            // on the connection or the cumulative acknowledgment point is beyond
//            // the last value of RecoveryPoint, a loss recovery phase SHOULD be
//            // initiated, per the fast retransmit algorithm outlined in [RFC2581].
//            // The following steps MUST be taken:
//            //
//            // (1) RecoveryPoint = HighData
//            //
//            // When the TCP sender receives a cumulative ACK for this data octet
//            // the loss recovery phase is terminated."
//
//            // RFC 3517, page 8: "If an RTO occurs during loss recovery as specified in this document,
//            // RecoveryPoint MUST be set to HighData.  Further, the new value of
//            // RecoveryPoint MUST be preserved and the loss recovery algorithm
//            // outlined in this document MUST be terminated.  In addition, a new
//            // recovery phase (as described in section 5) MUST NOT be initiated
//            // until HighACK is greater than or equal to the new value of
//            // RecoveryPoint."
//            if (state->recoveryPoint == 0 || seqGE(state->snd_una, state->recoveryPoint)) {    // HighACK = snd_una
//                state->recoveryPoint = state->snd_max;    // HighData = snd_max
//                state->lossRecovery = true;
//                EV_DETAIL << " recoveryPoint=" << state->recoveryPoint;
//            }
//        }
//        // RFC 2581, page 5:
//        // "After the fast retransmit algorithm sends what appears to be the
//        // missing segment, the "fast recovery" algorithm governs the
//        // transmission of new data until a non-duplicate ACK arrives.
//        // (...) the TCP sender can continue to transmit new
//        // segments (although transmission must continue using a reduced cwnd)."
//
//        // enter Fast Recovery
//        recalculateSlowStartThreshold();
//        // "set cwnd to ssthresh plus 3 * SMSS." (RFC 2581)
//        state->snd_cwnd = state->ssthresh + 3 * state->snd_mss;    // 20051129 (1)
//
//        conn->emit(cwndSignal, state->snd_cwnd);
//
//        EV_DETAIL << " set cwnd=" << state->snd_cwnd << ", ssthresh=" << state->ssthresh << "\n";
//
//        // Fast Retransmission: retransmit missing segment without waiting
//        // for the REXMIT timer to expire
//        conn->retransmitOneSegment(false);
//
//        // Do not restart REXMIT timer.
//        // Note: Restart of REXMIT timer on retransmission is not part of RFC 2581, however optional in RFC 3517 if sent during recovery.
//        // Resetting the REXMIT timer is discussed in RFC 2582/3782 (NewReno) and RFC 2988.
//
//        if (state->sack_enabled) {
//            // RFC 3517, page 7: "(4) Run SetPipe ()
//            //
//            // Set a "pipe" variable  to the number of outstanding octets
//            // currently "in the pipe"; this is the data which has been sent by
//            // the TCP sender but for which no cumulative or selective
//            // acknowledgment has been received and the data has not been
//            // determined to have been dropped in the network.  It is assumed
//            // that the data is still traversing the network path."
//            conn->setPipe();
//            // RFC 3517, page 7: "(5) In order to take advantage of potential additional available
//            // cwnd, proceed to step (C) below."
//            if (state->lossRecovery) {
//                // RFC 3517, page 9: "Therefore we give implementers the latitude to use the standard
//                // [RFC2988] style RTO management or, optionally, a more careful variant
//                // that re-arms the RTO timer on each retransmission that is sent during
//                // recovery MAY be used.  This provides a more conservative timer than
//                // specified in [RFC2988], and so may not always be an attractive
//                // alternative.  However, in some cases it may prevent needless
//                // retransmissions, go-back-N transmission and further reduction of the
//                // congestion window."
//                // Note: Restart of REXMIT timer on retransmission is not part of RFC 2581, however optional in RFC 3517 if sent during recovery.
//                EV_INFO << "Retransmission sent during recovery, restarting REXMIT timer.\n";
//                restartRexmitTimer();
//
//                // RFC 3517, page 7: "(C) If cwnd - pipe >= 1 SMSS the sender SHOULD transmit one or more
//                // segments as follows:"
//                if (((int)state->snd_cwnd - (int)state->pipe) >= (int)state->snd_mss) // Note: Typecast needed to avoid prohibited transmissions
//                    conn->sendDataDuringLossRecoveryPhase(state->snd_cwnd);
//            }
//        }
//
//        // try to transmit new segments (RFC 2581)
//        sendData(false);

        /////////////////*********  Added for sack support from TcpReno  *********/////////////////////







            if (!state->lossRecovery) {     /////////////////////////***** for sack support from TcpReno:   ***  !state->lossRecovery && !state->sack_enabled  ***   *****/////////////////////////
                // RFC 3782, page 4:
                // "1) Three duplicate ACKs:
                // When the third duplicate ACK is received and the sender is not
                // already in the Fast Recovery procedure, check to see if the
                // Cumulative Acknowledgement field covers more than "recover".  If
                // so, go to Step 1A.  Otherwise, go to Step 1B."
                //
                // RFC 3782, page 6:
                // "Step 1 specifies a check that the Cumulative Acknowledgement field
                // covers more than "recover".  Because the acknowledgement field
                // contains the sequence number that the sender next expects to receive,
                // the acknowledgement "ack_number" covers more than "recover" when:
                //      ack_number - 1 > recover;"
                if (state->snd_una - 1 > state->recover) {
                    EV_INFO << "CUBIC on dupAcks == DUPTHRESH(=3): perform Fast Retransmit, and enter Fast Recovery:";

                    // RFC 3782, page 4:
                    // "1A) Invoking Fast Retransmit:
                    // If so, then set ssthresh to no more than the value given in
                    // equation 1 below.  (This is equation 3 from [RFC2581]).
                    //      ssthresh = max (FlightSize / 2, 2*SMSS)           (1)
                    // In addition, record the highest sequence number transmitted in
                    // the variable "recover", and go to Step 2."


       //martino (from RFC 8312)  with these steps (CUBIC does not change Fast Recovery and Retransmit of standard TCP): restart epoch_start=0, record W_max, rcord epoch_start (new at loss event), deflate cwnd  with beta

                    recalculateSlowStartThreshold();            //**

    //                state->epoch_start = simTime();

    //                uint32_t flight_size = std::min(state->snd_cwnd, state->snd_wnd);      //Added        **
    //                state->ssthresh = std::max(flight_size / 2, 2 * state->snd_mss);

                    state->recover = (state->snd_max - 1);   //recover variable could be the max value of CWND "W_max" in CUBIC, before the loss event
                    state->firstPartialACK = false;
                    state->lossRecovery = true;
                    EV_INFO << " set recover=" << state->recover;


                    // RFC 3782, page 4:
                    // "2) Entering Fast Retransmit:
                    // Retransmit the lost segment and set cwnd to ssthresh plus 3 * SMSS.
                    // This artificially "inflates" the congestion window by the number
                    // of segments (three) that have left the network and the receiver
                    // has buffered."
                    state->snd_cwnd = state->ssthresh + 3 * state->snd_mss;

                    conn->emit(cwndSignal, state->snd_cwnd);

                    conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
                    conn->emit(ssthreshSignal, state->ssthresh);                   // added


                    EV_DETAIL << " , cwnd=" << state->snd_cwnd << ", ssthresh=" << state->ssthresh << "\n";
                    conn->retransmitOneSegment(false);
                   // RFC 3782, page 5:
                    // "4) Fast Recovery, continued:
                    // Transmit a segment, if allowed by the new value of cwnd and the
                    // receiver's advertised window."
                    sendData(false);
                }
                else {
                    EV_INFO << "CUBIC on dupAcks == DUPTHRESH(=3): not invoking Fast Retransmit and Fast Recovery\n";

                    // RFC 3782, page 4:
                    // "1B) Not invoking Fast Retransmit:
                    // Do not enter the Fast Retransmit and Fast Recovery procedure.  In
                    // particular, do not change ssthresh, do not go to Step 2 to
                    // retransmit the "lost" segment, and do not execute Step 3 upon
                    // subsequent duplicate ACKs."
                }
            }
            EV_INFO << "CUBIC on dupAcks == DUPTHRESH(=3): TCP is already in Fast Recovery procedure\n";
        }


    else if (state->dupacks > DUPTHRESH) {    // DUPTHRESH = 3
        if (state->lossRecovery) {
            // RFC 3782, page 4:
            // "3) Fast Recovery:
            // For each additional duplicate ACK received while in Fast
            // Recovery, increment cwnd by SMSS.  This artificially inflates the
            // congestion window in order to reflect the additional segment that
            // has left the network."
            state->snd_cwnd += state->snd_mss;

            conn->emit(cwndSignal, state->snd_cwnd);

            conn->emit(WmaxSignal,state->last_max_cwnd);                   // added
            conn->emit(ssthreshSignal, state->ssthresh);                   // added

            EV_DETAIL << "CUBIC on dupAcks > DUPTHRESH(=3): Fast Recovery: inflating cwnd by SMSS, new cwnd=" << state->snd_cwnd << "\n";

            // RFC 3782, page 5:
            // "4) Fast Recovery, continued:
            // Transmit a segment, if allowed by the new value of cwnd and the
            // receiver's advertised window."
            sendData(false);
        }
    }
}






void TcpCubic::dataSent(uint32_t fromseq) {
    TcpBaseAlg::dataSent(fromseq);

    // save the time the packet was sent
    // fromseq is the seq number of the 1st sent byte

    simtime_t sendtime = simTime();
    state->regions.clearTo(state->snd_una);
    state->regions.set(fromseq, state->snd_max, sendtime, state->cubicDelivered, state->lastTimeDelivered);
                    // Before was Commented due to a modification in the regions
                    // getter and setter of TcpSegmentTransmitInfoList, uncomment if eventually modify it again!
                    // ### Now uncommented and included 2 new variables to be compliant with BBR Implementation

}


void TcpCubic::segmentRetransmitted(uint32_t fromseq, uint32_t toseq)
{
    TcpBaseAlg::segmentRetransmitted(fromseq, toseq);

//    state->regions.set(fromseq, toseq, simTime());
                // Commented due to a modification in the regions
                // getter and setter of TcpSegmentTransmitInfoList, uncomment if eventually modify it again
}


//void TcpCubic::cubictcp_cwnd_event(TcpCubicStateVariables *& state, TcpEventCode& event)
//{
//    if (event == TCP_E_SEND) {
//        //simtime_t epoch_start = 0;
//        simtime_t now = simTime();
//        simtime_t delta;
//
//       delta = now - state->time_last_data_sent;   //last_time;
//
//
//
//        /* We were application limited (idle) for a while.
//         * Shift epoch_start to keep cwnd growth to cubic curve.
//         */
//        if (state->epoch_start > SIMTIME_ZERO && delta > SIMTIME_ZERO) {
//            state->epoch_start += delta;
//            if (state->epoch_start > SIMTIME_ZERO && now > state->epoch_start) {  //SIMTIME_ZERO = 0
//                state->epoch_start = now;
//        }
//        return;
//        }
//    }
//}


/* calculate the cubic root of x using a table lookup followed by one
 * Newton-Raphson iteration.
 * Avg err ~= 0.195%
 */

uint32_t TcpCubic::cubic_root(uint64_t a){
    uint32_t x, b, shift;
    /*
     * cbrt(x) MSB values for x MSB values in [0..63].
     * Precomputed then refined by hand - Willy Tarreau
     *
     * For x in [0..63],
     *   v = cbrt(x << 18) - 1
     *   cbrt(x) = (v[x] + 10) >> 6
     */
    static const uint8_t v[] = {
        /* 0x00 */    0,   54,   54,   54,  118,  118,  118,  118,
        /* 0x08 */  123,  129,  134,  138,  143,  147,  151,  156,
        /* 0x10 */  157,  161,  164,  168,  170,  173,  176,  179,
        /* 0x18 */  181,  185,  187,  190,  192,  194,  197,  199,
        /* 0x20 */  200,  202,  204,  206,  209,  211,  213,  215,
        /* 0x28 */  217,  219,  221,  222,  224,  225,  227,  229,
        /* 0x30 */  231,  232,  234,  236,  237,  239,  240,  242,
        /* 0x38 */  244,  245,  246,  248,  250,  251,  252,  254,
    };

//    int b
    b = static_cast<int>(std::log2(a & -a)) + 1;
    if (b < 7) {
        /* a in [0..63] */
        return ((uint32_t)v[(uint32_t)a] + 35) >> 6;
    }

    b = ((b * 84) >> 8) - 1;
    shift = (a >> (b * 3));

    x = ((uint32_t)(((uint32_t)v[shift] + 10) << b)) >> 6;

    /*
     * Newton-Raphson iteration
     *                         2
     * x    = ( 2 * x  +  a / x  ) / 3
     *  k+1          k         k
     */
    x = (2 * x + static_cast<uint32_t>(inet::math::div(a, static_cast<uint64>(x) * static_cast<uint64>(x - 1))));
    //x = (2 * x + (uint32_t_t)div64_u64(a, (uint64_t)x * (uint64_t)(x - 1)));   linux version
    x = ((x * 341) >> 10);
    return x;
}



void TcpCubic::tcpFriendlinessLogic() {   //pobably the passed variable "uint32_t  ack_cnt" is not relevant            (TcpCubicStateVariables *& state, uint32_t ack_cnt)
//    if (tcp_friendliness) {
//        uint32_t scale = state->beta_scale;
//        uint32_t delta, max_cnt;
//        simtime_t t = simTime() - state->epoch_start;
//
//        delta = (state->snd_cwnd * scale) >> 3;
//        while (state->ack_cnt > delta) {
//            state->ack_cnt -= delta;
//            state->rcv_adv++;               //rcv_adv = tcp_cwnd
//
//            state->snd_cwnd = (state->last_max_cwnd * state->beta) + (3*(1-state->beta)/(1+state->beta)) * (t/state->srtt);  //martino
//        }
//
//        if (state->rcv_adv > state->snd_cwnd) {
//            delta = state->rcv_adv - state->snd_cwnd;
//            max_cnt = state->snd_cwnd / delta;
//            if (state->cnt > max_cnt)
//                state->cnt = max_cnt;
//        }
//    }
//}

    double const_cal = ((3.0 * 0.2)/(2.0 - 0.2));
    double mul = ((double)(state->ack_cnt * state->snd_mss))/((double)state->snd_cwnd);
    double resolt_mul = const_cal * mul;
    state->tcp_cwnd = state->tcp_cwnd + static_cast<uint32_t>(resolt_mul);
    state->ack_cnt = 0;
    if(state->tcp_cwnd > state->snd_cwnd)
    {
        uint32_t max_cnt = (state->snd_cwnd / (state->tcp_cwnd - state->snd_cwnd));           //  (state->tcp_cwnd - state->snd_cwnd))/state->snd_mss
        if( state->cnt > max_cnt)
        {
            state->cnt = max_cnt;
        }
    }
}



/*
 * Compute congestion window to use in Congestion Avoidance.          "void"
 */      //   (TcpCubicStateVariables *& state, uint32_t acked)
void TcpCubic::bictcp_update()    //check this method and eventually call it from the AckReceieved function above , in congAvoidance and when a full segment Ack is received
{
    uint32_t bic_target, max_cnt;
//    uint64 offs;  //uint64

    double delta;
    simtime_t offs;
    simtime_t t;

//    simtime_t jiffies = simTime();


//    state->last_cwnd = state->snd_cwnd;              // before declared after tcpFriendlinessLogic
//    state->last_time = simTime(); //simTime();
//
//    simtime_t simResolution = SIMTIME_PRECISION;  // Simulation time resolution
//
//    simtime_t jiffies = simResolution/HZ;  // 1/HZ , 0.01,  HZ = 100, HZ = 250

    state->ack_cnt++;   /* count the number of ACKed packets */

//    if (state->last_max_cwnd == state->snd_cwnd && (jiffies - state->last_time) <= HZ/32)  //<= simTime()
//
//        std::cout << "Exiting bictcp_update, jiffies : "<< jiffies << ", Last Time: " << state->last_time << std::endl;
//
//        return;

    /* The CUBIC function can update ca->cnt at most once per jiffy.
     * On all cwnd reduction events, ca->epoch_start is set to 0,
     * which will force a recalculation of ca->cnt.
     */

//    if (state->epoch_start > 0 && jiffies == state->last_time)  //SIMTIME_ZERO = 0    ,   simTime() == state->last_time
////        goto tcp_friendliness;    removed got by adding te new method TcpFriendliness
//        tcp_friendliness = true;
//          tcpFriendlinessLogic(state, state->ack_cnt);





    if (state->epoch_start == SIMTIME_ZERO) {    //SIMTIME_ZERO = 0
        state->epoch_start = simTime();  // Record the beginning     ,  simTime()
//        state->ack_cnt = acked;          // Start counting

//        state->rcv_adv = state->snd_cwnd;          // Synchronize with cubic             **
//        state->tcp_cwnd = state->snd_cwnd;                                                 //**

        if ((state->last_max_cwnd) < ( state->snd_cwnd)) {
//           state->bic_K = 0.0;
            state->K = SIMTIME_ZERO; //SimTime(state->bic_K);
            state->bic_origin_point = state->snd_cwnd;
        } else {
            /* Compute new K based on
             * (wmax-cwnd) * (srtt>>3 / HZ) / c * 2^(3*bictcp_HZ)
             */
//            state->bic_K = cubic_root(state->cube_factor * (state->last_max_cwnd - state->snd_cwnd));
//            state->bic_origin_point = state->last_max_cwnd;
//            state->K = SimTime(state->bic_K); //convet to SimTime

            double dividend = static_cast<double>(state->last_max_cwnd - state->snd_cwnd);
            dividend = dividend/state->snd_mss; //convert to segments
            double cubic_root = dividend / state->c;
            state->bic_K = std::cbrtl(cubic_root);
            state->K = SimTime(state->bic_K);
            state->bic_origin_point = state->last_max_cwnd;
        }

        state->ack_cnt = 1;
        state->tcp_cwnd = state->snd_cwnd;
    }

    /* cubic function - calc*/
    /* calculate c * time^3 / rtt,
     *  while considering overflow in calculation of time^3
     * (so time^3 is done by using 64 bit)
     * and without the support of division of 64bit numbers
     * (so all divisions are done by using 32 bit)
     *  also NOTE the unit of those variables
     *    time  = (t - K) / 2^bictcp_HZ
     *    c = bic_scale >> 10
     * rtt  = (srtt >> 3) / HZ
     * !!! The following code does not have overflow problems,
     * if the cwnd < 1 million packets !!!
     */

    t = (simTime() + state->minRTT - state->epoch_start);  // Assuming epoch_start is in simtime_t  , before jiffies = simTime()     .inUnit(SIMTIME_US)

    state->c_t = t;

    //    t += usecs_to_jiffies(state->delay_min);

    /* change the unit from HZ to bictcp_HZ */
//    t *= pow(2,BICTCP_HZ);

//    std::cout<<"t: "<< t << std::endl;                           //*                 // DEBUG

//    do_div(t, HZ);

//    double d = t/static_cast<double>(HZ);
//    t=inet::math::div(t, HZ);

    //here K = state->bic_K
    if (t < state->K)
        offs = state->K - t;
    else
        offs = t - state->K;

    double d = SIMTIME_DBL(offs);;
//    delta = (cubeRttScale * offs * offs * offs) >> (10 + 3 * BICTCP_HZ); //  /(1 << (10 + 3 * BICTCP_HZ)); //>> (10 + 3 * BICTCP_HZ);    .dbl()
      delta = std::pow(d , 3.0);

    //  origin_point + C*(t-K)^3
    uint32_t factor =  static_cast<uint32_t>(state->c * delta);

//    std::cout<<"bic_origin_point: "<< state->bic_origin_point << std::endl;                           //*                // DEBUG

    if (t < state->K)
        bic_target = state->bic_origin_point/state->snd_mss - factor;
    else
        bic_target = state->bic_origin_point/state->snd_mss + factor;

    /* cubic function - calc bictcp_cnt*/

    if (bic_target > state->snd_cwnd/state->snd_mss)   //could be                       state->snd_cwnd/state->snd_mss
        state->cnt = state->snd_cwnd/state->snd_mss / (bic_target - (state->snd_cwnd/state->snd_mss));
    else
        state->cnt = 100 * (state->snd_cwnd/state->snd_mss);  // very small increment


//    // The initial growth of cubic function may be too conservative
//    // when the available bandwidth is still unknown.
//    if (state->last_max_cwnd == 0 && state->cnt > 20)
//        state->cnt = 20;   // increase cwnd 5% per RTT


    if (state->tcp_friendliness == true)

        tcpFriendlinessLogic();   //state, state->ack_cnt


    /*commented since added the new method "tcpFriendlinessLogic"
//    // TCP Friendliness
//    tcp_friendliness:
//    if (tcp_friendliness) {
//        uint32_t_t scale = BICTCP_BETA_SCALE;  // Assuming beta_scale is a variable
//
//        delta = (state->snd_cwnd * scale) >> 3;
//        while (state->ack_cnt > delta) {
//            state->ack_cnt -= delta;
//            state->tcp_cwnd++;
//        }
//
//        if (state->tcp_cwnd > state->snd_cwnd) {
//            delta = state->tcp_cwnd - state->snd_cwnd;
//            max_cnt = state->snd_cwnd / delta;
//            if (state->cnt > max_cnt)
//                state->cnt = max_cnt;
//        }
//    }

    */

    // The maximum rate of cwnd increase CUBIC allows is 1 packet per
    // 2 packets ACKed, meaning cwnd grows at 1.5x per RTT.

//    state-> cnt = std::max(state->cnt, uint32_t(2));  //static_cast<uint32_t_t>(2)     //2U

}


//uint32_t TcpCubic::cubicRoot(double x) {
//    return std::cbrt(x);
//}
//
//uint32_t TcpCubic::calculateK() {
//    return cubicRoot(state->last_max_cwnd * (1 - state->beta) / state->c);
//}
//
//uint32_t TcpCubic::W_cubic(double t) {
//    double K = calculateK();
//    state->c_K = SimTime(K); //convet to SimTime
//    return state->c * std::pow((t - K), 3) + state->last_max_cwnd;
//}



void TcpCubic::debugCubic()
{
    EV_DETAIL << "============================================"<< endl
       <<"Time     " << simTime() << endl
       <<"Cwnd now " << state->snd_cwnd <<endl
       <<"ssthresh " << state->ssthresh <<endl
       <<"Min RTT  " << state->minRTT << endl
//       <<"target   " << state->bic_target<< endl
       <<"K        " << state->K <<endl
//       <<"t        " << state->t <<endl
//       <<"offset    " << state->offs <<endl
       <<"c_cnt    " << state->ack_cnt << endl
//       <<"cwnd_cnt " << state->c_cwnd_cnt << endl
       << "============================================"
       << endl;
}




