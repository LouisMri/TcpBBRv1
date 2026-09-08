// SPDX-FileCopyrightText: 2026 Friedrich-Alexander University Erlangen-Nuernberg (FAU), Computer Science 7 - Computer Networks and Communication Systems
// SPDX-License-Identifier: LGPL-3.0-or-later

/*
 * TcpBBRv1.h
 *
 *  Created on: Apr 10, 2024
 *      Author: Luigi Martino
 */

#ifndef INET_TRANSPORTLAYER_TCP_FLAVOURS_TCPBBRV1_H_
#define INET_TRANSPORTLAYER_TCP_FLAVOURS_TCPBBRV1_H_

#include <utility>
#include <omnetpp.h>
#include "inet/transportlayer/tcp/flavours/TcpBaseAlg.h"
#include "inet/transportlayer/tcp/flavours/windowed-filter.h"
#include "TcpSegmentTransmitInfoList.h"



using namespace omnetpp;
using namespace inet;
using namespace tcp;


class INET_API TcpBBRv1StateVariables : public TcpBaseAlgStateVariables {

protected:



public:
    TcpBBRv1StateVariables();
    ~TcpBBRv1StateVariables();
    virtual std::string str() const override;
    virtual std::string detailedInfo() const override;
    TcpSegmentTransmitInfoList regions;





    enum BbrMode_t {
        BBR_STARTUP,
        BBR_DRAIN,
        BBR_PROBE_BW,
        BBR_PROBE_RTT,
    };





    using RateType = decltype(std::declval<omnetpp::cDatarateChannel>().getDatarate());


    typedef ::WindowedFilter<double,
            ::MaxFilter<double>,
                           uint32_t,
                           uint32_t>
        MaxBandwidthFilter_t;


    simtime_t min_rtt_stamp_;
    simtime_t probe_rtt_done_stamp_;
    bool probe_rtt_round_done_;
    uint32 prior_cwnd_;
    bool idle_restart_;
    simtime_t extra_acked_interval_start_;
    uint32 extra_acked_delivered_;

    double highGain;

    uint32 ssthresh;
    uint32 m_ssThresh;

    bool v_inc_flag;
    bool v_incr_ss;
    int32 v_incr;
    uint32 v_worried;


      double pacing_rate_;
      double pacing_gain_;
      double cwnd_gain_;
      double min_rtt_;
      double bdp_;
      double full_bw_;
      double max_bw_;
      bool probe_rtt_mode_;



      double pacing_rate_prev_;
      int round_count_;
      simtime_t last_packet_sent_time_;






      MaxBandwidthFilter_t m_maxBwFilter;
      uint32_t m_bandwidthWindowLength = 0;
      BbrMode_t m_state = BBR_STARTUP;



      double m_pacingGain;
      double m_cWndGain;
      double m_highGain;
      bool m_isPipeFilled = false;
      uint32_t m_minPipeCwnd = 0;
      uint32_t m_roundCount = 0;
      bool m_roundStart = false;
      uint32_t m_nextRoundDelivered = 0;
      uint32_t m_priorDelivered = 0;
      simtime_t m_probeRttDuration = SimTime(200, SIMTIME_MS);

      simtime_t priorDelivTime = SIMTIME_ZERO;
      simtime_t m_probeRtPropStamp;

      simtime_t m_probeRttDoneStamp;
      bool m_probeRttRoundDone = false;
      bool m_packetConservation = false;
      uint32_t m_priorCwnd;
      bool m_idleRestart = false;
      uint32_t m_targetCWnd = 0;
      uint64_t m_fullBandwidth;
      uint32_t m_fullBandwidthCount = 0;
      simtime_t m_minRtt;

      uint32_t m_sendQuantum = 0;
      simtime_t m_cycleStamp;
      uint32_t m_cycleIndex = 0;
      bool m_minRttExpired = false;
      simtime_t m_minRttFilterLen = SimTime(10, SIMTIME_S);

      simtime_t m_minRttStamp = SIMTIME_ZERO;
      bool m_isInitialized = false;

      uint32_t m_rngStream = 0;


      uint64_t m_delivered = 0;

      uint32_t m_extraAckedGain = 1;
      uint32_t m_extraAcked[2] = {0, 0};
      uint32_t m_extraAckedWinRtt = 0;
      uint32_t m_extraAckedWinRttLength = 5;
      uint32_t m_ackEpochAckedResetThresh = (1 << 17);
      uint32_t m_extraAckedIdx = 0;
      simtime_t m_ackEpochTime;
      uint32_t m_ackEpochAcked = 0;
      bool m_hasSeenRtt = false;
      double m_pacingMargin = 0.01;


      simtime_t m_lastRtt = SIMTIME_ZERO;

      uint32_t m_bytesInFlight = 0;
      uint32_t  ack_cnt;

      void setMode(BbrMode_t mode) { m_state = mode; }
      uint32_t stream;
      uint32_t m_appLimited;
      uint64_t m_appLimitedUntil = 0;
      uint32_t lastAckNo;
      uint32_t delivered;
      uint32_t m_initialCWnd;
      uint32_t lastAcked = 0;
      double m_deliveryRate = 0;

      simtime_t lastDeliveredTime;
      uint32 bbrlast_ack_sent=0;



      bool m_pacing = false;

private:



};



class INET_API TcpBBRv1 : public TcpBaseAlg {

  protected:

    TcpBBRv1StateVariables *& state;

    TcpSendQueue *sendQueue = nullptr;
    TcpHeader *tcpHeader;


    Tcp *tcpMain = nullptr;

    static  simsignal_t bbrModeSignal;
    static simsignal_t inflightSignal;
    static simsignal_t maxBwSignal;
    static simsignal_t minRttSignal;



       virtual TcpStateVariables *createStateVariables() override
       {
           return new TcpBBRv1StateVariables();
       }




private:
       simtime_t currentTime;



  public:

    TcpBBRv1();

    virtual void initialize() override;
    virtual void receivedDataAck(uint32 firstSeqAcked) override;
    void receivedDuplicateAck() override;
    void segmentRetransmitted(uint32 fromseq, uint32 toseq) override;

    bool trySendOnePacedSegmentOrSchedule(bool sendCommandInvoked);
    bool sendData(bool sendCommandInvoked);
    void dataSent(uint32_t fromseq) override;
    void EnterRecovery();

    void EnterLossRto();
    void OnTxStart(TcpBBRv1StateVariables *&state);
    virtual void processRexmitTimer(TcpEventCode& event) override;


    uint32_t GetSsThresh(TcpBBRv1StateVariables *& state);
    double calculateBDP();
    void debugBBR();

    void SetPacingRate(TcpBBRv1StateVariables *& state, double gain);
    void ExitProbeRTT();
    void EnterProbeBW();
    void EnterDrain();
    void SetBbrState(TcpBBRv1StateVariables::BbrMode_t mode);
    void EnterStartup();

    void InitPacingRate(TcpBBRv1StateVariables *& state);
    simtime_t MilliSeconds(uint64_t value)
    { return  SimTime(value, SIMTIME_MS); }
    double GetPacingGain();
    void AdvanceCyclePhase();
    void SetStream(uint32_t stream);
    void InitRoundCounting();
    void InitFullPipe();
    void HandleRestartFromIdle(TcpBBRv1StateVariables *& state);
    uint32_t InFlight(TcpBBRv1StateVariables *& state, double gain);
    bool IsNextCyclePhase(TcpBBRv1StateVariables *& state);
    void CheckCyclePhase(TcpBBRv1StateVariables *& state);
    void CheckFullPipe(TcpBBRv1StateVariables *& state);


    void CheckDrain(TcpBBRv1StateVariables *& state);
    void UpdateRTprop(TcpBBRv1StateVariables *& state);
    void EnterProbeRTT();
    void SaveCwnd(TcpBBRv1StateVariables *& state);
    void HandleProbeRTT(TcpBBRv1StateVariables *& state);
    void RestoreCwnd(TcpBBRv1StateVariables *& state);

    void UpdateAppLimited();
    void SetAppLimited();


    static const char* modeName(TcpBBRv1StateVariables::BbrMode_t mode);
    void CongControl(TcpBBRv1StateVariables *& state, uint32 firstSeqAcked);
    uint32_t GetBbrState();
    double GetCwndGain();
    void CheckProbeRTT(TcpBBRv1StateVariables *& state);
    void SetSendQuantum(TcpBBRv1StateVariables *& state);
    void UpdateTargetCwnd(TcpBBRv1StateVariables *& state);
    uint32_t AckAggregationCwnd();
    void UpdateAckAggregation(TcpBBRv1StateVariables *& state);
    bool ModulateCwndForRecovery(TcpBBRv1StateVariables *& state);
    void ModulateCwndForProbeRTT(TcpBBRv1StateVariables *& state);
    void SetCwnd(TcpBBRv1StateVariables *& state);
    void UpdateRound(TcpBBRv1StateVariables *& state);
    void UpdateBottleneckBandwidth(TcpBBRv1StateVariables *& state);
    void UpdateModelAndState(TcpBBRv1StateVariables *& state);
    void UpdateControlParameters(TcpBBRv1StateVariables *& state);

};




#endif
