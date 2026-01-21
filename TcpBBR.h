/*
 * TcpBBR.h
 *
 *  Created on: Apr 10, 2024
 *      Author: martino
 */

#ifndef INET_TRANSPORTLAYER_TCP_FLAVOURS_TCPBBR_H_
#define INET_TRANSPORTLAYER_TCP_FLAVOURS_TCPBBR_H_

#include <utility>    // for std::declval
#include <omnetpp.h>
#include "inet/transportlayer/tcp/flavours/TcpBaseAlg.h"
#include "inet/transportlayer/tcp/flavours/windowed-filter.h"
#include "TcpSegmentTransmitInfoList.h"



using namespace omnetpp;
using namespace inet;
using namespace tcp;


class INET_API TcpBBRStateVariables : public TcpBaseAlgStateVariables {

protected:


public:
    TcpBBRStateVariables();
    ~TcpBBRStateVariables();
    virtual std::string str() const override;
    virtual std::string detailedInfo() const override;
    TcpSegmentTransmitInfoList regions;




    /* BBR has the following modes for deciding how fast to send: */
    enum BbrMode_t {
        BBR_STARTUP,    /* ramp up sending rate rapidly to fill pipe */
        BBR_DRAIN,  /* drain any queue created during startup */
        BBR_PROBE_BW,   /* discover, share bw: pace around estimated bw */
        BBR_PROBE_RTT,  /* cut inflight to min to probe min_rtt */
    };

    /**
     * @brief Literal names of BBR mode for use in log messages
     */
//    static const char* const BbrModeName[BBR_PROBE_RTT + 1];


    using RateType = decltype(std::declval<omnetpp::cDatarateChannel>().getDatarate());    // instead of "RateType" one can also use "double"


    typedef ::WindowedFilter<double,                // measurement type = double (bps)   ; double or RateType
            ::MaxFilter<double>,                 // comparator for double
                           uint32_t,              // TimeT (kept as in original)
                           uint32_t>              // TimeDeltaT (kept as in original)
        MaxBandwidthFilter_t;






//    typedef WindowedFilter<DatnominalBandwidthaRate,
//                              MaxFilter<DataRate>,
//                              uint32_t,
//                              uint32_t>
//           MaxBandwidthFilter_t;        //!< Definition of max bandwidth filter.



//    uint32 v_recoverypoint;
//    simtime_t v_cwnd_changed;    // last time cwnd changes because of a rtx.
//
//    simtime_t v_baseRTT;
//    simtime_t v_sumRTT;    // sum of rtt's measured within one RTT
//    int v_cntRTT;    // # of rtt's measured within one RTT
//    uint32 v_begseq;    // register next pkt to be sent,for rtt calculation in receivedDataAck
//    simtime_t v_begtime;    // register time for rtt calculation
//
//    simtime_t v_rtt_timeout;    // vegas fine-grained timeout
//    simtime_t v_sa;    // average for vegas fine-grained timeout
//    simtime_t v_sd;    // deviation for vegas fine-grained timeout

//    TcpSegmentTransmitInfoList regions;

    simtime_t min_rtt_stamp_;
    simtime_t probe_rtt_done_stamp_;
    bool probe_rtt_round_done_;
    uint32 prior_cwnd_;
    bool idle_restart_;
    simtime_t extra_acked_interval_start_;
    uint32 extra_acked_delivered_;
//    double pacing_gain_;
//    double cwnd_gain_;
    double highGain;
//    simtime_t min_rtt_;

    uint32 ssthresh;    ///< slow start threshold
    uint32 m_ssThresh;

    bool v_inc_flag;    // for slow start: "exponential growth only every other RTT"
    bool v_incr_ss;    // to control no incr. cwnd if during slowstart ssthresh has been exceeded before the rtt is over
    int32 v_incr;    // incr/decr
    uint32 v_worried;    // pkts a to retransmit due to vegas fine-grained timeout

    // BBR-specific variables ###############################################   Probably redundant and maybe later delete them
      double pacing_rate_;
      double pacing_gain_;   // Pacing gain for determining the pacing rate
      double cwnd_gain_;     // Congestion window gain for determining the congestion window size
      double min_rtt_;       // Minimum Round-Trip Time (RTT) seen during the connection
      double bdp_;           // Bandwidth Delay Product (BDP) estimation
      double full_bw_;       // Estimated full bandwidth of the network
      double max_bw_;        // Maximum bandwidth seen during the connection
      bool probe_rtt_mode_;  // Flag indicating if BBR is in the RTT probing mode


      // BBR-specific variables for bandwidth probing
      double pacing_rate_prev_;  // Pacing rate before entering probeRTT mode
      int round_count_;          // Number of round trips since entering probeRTT mode
      simtime_t last_packet_sent_time_;  // Time of last probe packet sent

    ////////////////////// #######################################################     Probably redundant and maybe later delete them




      MaxBandwidthFilter_t m_maxBwFilter;        //!< Maximum bandwidth filter
      uint32_t m_bandwidthWindowLength = 0; //!< A constant specifying the length of the BBR.BtlBw max
      BbrMode_t m_state = BBR_STARTUP; //!< Current state of BBR state machine                //!< filter window, default 10 packet-timed round trips.
      uint32_t m_inflight = 0;


      double m_pacingGain; //!< The dynamic pacing gain factor
      double m_cWndGain;   //!< The dynamic congestion window gain factor
      double m_highGain;   //!< A constant specifying highest gain factor, default is 2.89
      bool m_isPipeFilled = false; //!< A boolean that records whether BBR has filled the pipe
      uint32_t m_minPipeCwnd = 0; //!< The minimal congestion window value BBR tries to target, default 4 Segment size
      uint32_t m_roundCount = 0; //!< Count of packet-timed round trips
      bool m_roundStart = false; //!< A boolean that BBR sets to true once per packet-timed round trip
      uint32_t m_nextRoundDelivered = 0;           //!< Denotes the end of a packet-timed round trip
      uint32_t m_priorDelivered = 0;
      simtime_t m_probeRttDuration = SimTime(200, SIMTIME_MS); //!< A constant specifying the minimum duration for
                                                               //!< which ProbeRTT state, default 200 millisecs
      simtime_t m_probeRtPropStamp;  //!< The wall clock time at which the current BBR.RTProp sample was
                                     //!< obtained.
      simtime_t m_probeRttDoneStamp; //!< Time to exit from BBR_PROBE_RTT state
      bool m_probeRttRoundDone = false;  //!< True when it is time to exit BBR_PROBE_RTT
      bool m_packetConservation = false; //!< Enable/Disable packet conservation mode
      uint32_t m_priorCwnd = 0;          //!< The last-known good congestion window
      bool m_idleRestart = false;        //!< When restarting from idle, set it true
      uint32_t m_targetCWnd = 0; //!< Target value for congestion window, adapted to the estimated BDP
      uint64_t m_fullBandwidth;      //!< Value of full bandwidth recorded
      uint32_t m_fullBandwidthCount = 0; //!< Count of full bandwidth recorded consistently
      simtime_t m_minRtt; // = SimTime::getMaxTime(); //!< Estimated two-way round-trip propagation delay of the path, estimated
                                                  //!< from the windowed minimum recent round-trip delay sample.
      uint32_t m_sendQuantum = 0;            //!< The maximum size of a data aggregate scheduled and transmitted together
      simtime_t m_cycleStamp; //!< Last time gain cycle updated
      uint32_t m_cycleIndex = 0;            //!< Current index of gain cycle
      bool m_minRttExpired = false;         //!< A boolean recording whether the BBR.RTprop has expired
      simtime_t m_minRttFilterLen = SimTime(10, SIMTIME_S); //!< A constant specifying the length of the RTProp min
                                                            //!< filter window, default 10 secs.
      simtime_t m_minRttStamp;              //!< The wall clock time at which the current BBR.RTProp sample was obtained
      bool m_isInitialized = false;         //!< Set to true after first time initialization variables
//      cUniform* m_uv = nullptr;             //!< Uniform Random Variable (use cUniform in OMNeT++)  // cUniform  ,  cRNG
//      cRNG* m_uv;                 // if m_uv is declared as a cRNG type then it is used in the .cc from intuniform(), if declared as nullptr, gives error
      uint32_t m_rngStream = 0;

      //      uint64_t m_uv = intRand(0);
      uint64_t m_delivered = 0;              //!< The total amount of data in bytes delivered so far
      // Ptr<TcpRateOps> m_rateOps;          //!< Rate operations (define as pointer or class if needed)
      uint32_t m_extraAckedGain = 1;         //!< Gain factor for adding extra ack to cwnd
      uint32_t m_extraAcked[2] = {0, 0};     //!< Maximum excess data acked in epoch
      uint32_t m_extraAckedWinRtt = 0;       //!< Age of extra acked in rtt
      uint32_t m_extraAckedWinRttLength = 5; //!< Window length of extra acked window
      uint32_t m_ackEpochAckedResetThresh = (1 << 17); //!< Max allowed val for m_ackEpochAcked, after which sampling epoch is reset
      uint32_t m_extraAckedIdx = 0; //!< Current index in extra acked array
      simtime_t m_ackEpochTime;      //!< Starting of ACK sampling epoch time
      uint32_t m_ackEpochAcked = 0;  //!< Bytes ACked in sampling epoch
      bool m_hasSeenRtt = false;     //!< Have we seen RTT sample yet?
      double m_pacingMargin = 0.01;  //!< BBR intentionally reduces the pacing rate by 1% to drain any
                                     //!< standing queues. See `bbr_rate_bytes_per_sec` in Linux.

      simtime_t m_lastRtt = SIMTIME_ZERO;

      uint32_t m_bytesInFlight;

      void setMode(BbrMode_t mode) { m_state = mode; } // method created for eventually setting the mode, not sure if will be used, in case not, remove later
      uint32_t stream;
      uint32_t m_appLimited;
      uint32_t lastAckNo;
      uint32_t delivered;
      uint32_t m_initialCWnd;


//      uint32_t m_bytesLoss;    // already defined in TcpConnection.h



private:



};



class INET_API TcpBBR : public TcpBaseAlg {

  protected:

    TcpBBRStateVariables *& state;    // alias to TcpAlgorithm's 'state'
    // TCP queues
    TcpSendQueue *sendQueue = nullptr;                // pointer to the  TcpSendQueue  class is necessary if i want to use a method or variable of that class!
    TcpHeader *tcpHeader;    // lastAckNo
//    TcpConnection *conn;



    Tcp *tcpMain = nullptr;    // Tcp module


    /** Create and return a TCPvegasStateVariables object. */
       virtual TcpStateVariables *createStateVariables() override
       {
           return new TcpBBRStateVariables();
       }


private:
       simtime_t currentTime; // current time in simulation



  public:
    /** Ctor */
    TcpBBR();

//    void initializeRNG(cRNG* rng) { state->m_uv = rng; }
    virtual void initialize() override;
    virtual void receivedDataAck(uint32 firstSeqAcked) override;
    void dataSent(uint32_t fromseq) override;    // const Ptr<const TcpHeader>& ack
    virtual void processRexmitTimer(TcpEventCode& event) override;
//    virtual void rttMeasurementComplete(simtime_t tSent, simtime_t tAcked);   //override      //  (const simtime_t& rtt)

    // Helper methods specific to BBR
    uint32_t GetSsThresh(TcpBBRStateVariables *& state, uint32_t bytesInFlight);
//    void updatePacingRate();   // method removed
//    void updateCongestionWindow();
    double calculateBDP();
//    void startProbeRTT();
//    void handleProbeRTT();
//    void sendProbePacket();
//    void updateProbeRTT();
    void debugBBR();

    void cwndEvent(TcpCAEvent_t& event, TcpConnection* conn, TcpBBRStateVariables *& state);
//    void restoreCwnd(TcpBBRStateVariables *& state);
    void SetPacingRate(TcpBBRStateVariables *& state, double gain);
    void ExitProbeRTT();
    void EnterProbeBW();
    void EnterDrain();
    void SetBbrState(TcpBBRStateVariables::BbrMode_t mode); // BbrMode_t state  ,  TcpBBRStateVariables *& state
    void EnterStartup();

    void InitPacingRate(TcpBBRStateVariables *& state);
    simtime_t MilliSeconds(uint64_t value)    // returns an integer value indicating the time as a time in (ms)
    { return  SimTime(value, SIMTIME_MS); }
    double GetPacingGain();
    void AdvanceCyclePhase();
    void SetStream(uint32_t stream);   //cRNG*
    void InitRoundCounting();
    void InitFullPipe();
    void HandleRestartFromIdle(TcpBBRStateVariables *& state);
    uint32_t InFlight(TcpBBRStateVariables *& state, double gain);
    bool IsNextCyclePhase(TcpBBRStateVariables *& state);
    void CheckCyclePhase(TcpBBRStateVariables *& state);
    void CheckFullPipe(TcpBBRStateVariables *& state);


    void CheckAppStatus();   // uint32 fromSeq

    void CheckDrain(TcpBBRStateVariables *& state);
    void UpdateRTprop(TcpBBRStateVariables *& state);
    void EnterProbeRTT();
    void SaveCwnd(TcpBBRStateVariables *& state);
    void HandleProbeRTT(TcpBBRStateVariables *& state);
    void RestoreCwnd(TcpBBRStateVariables *& state);
    void SetAppLimited(uint32_t bytesInFlight);


    static const char* modeName(TcpBBRStateVariables::BbrMode_t mode);
    void CongestionStateSet(TcpBBRStateVariables *& state);  //, TcpCAEvent_t& newState, TcpEventCode& event);  //TcpCAEvent_t
    void CongControl(TcpBBRStateVariables *& state, uint32 firstSeqAcked);
    uint32_t GetBbrState();
    double GetCwndGain();
    void CheckProbeRTT(TcpBBRStateVariables *& state);
    void SetSendQuantum(TcpBBRStateVariables *& state);
    void UpdateTargetCwnd(TcpBBRStateVariables *& state);
    uint32_t AckAggregationCwnd();
    void UpdateAckAggregation(TcpBBRStateVariables *& state);
    bool ModulateCwndForRecovery(TcpBBRStateVariables *& state);
    void ModulateCwndForProbeRTT(TcpBBRStateVariables *& state);
    void SetCwnd(TcpBBRStateVariables *& state);
    void UpdateRound(TcpBBRStateVariables *& state);
    void UpdateBottleneckBandwidth(TcpBBRStateVariables *& state);
    void UpdateModelAndState(TcpBBRStateVariables *& state);
    void UpdateControlParameters(TcpBBRStateVariables *& state);

};




#endif /* INET_TRANSPORTLAYER_TCP_FLAVOURS_TCPBBR_H_ */
