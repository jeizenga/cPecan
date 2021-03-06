#ifndef CONTINUOUS_HMM_H
#define CONTINUOUS_HMM_H

#include "stateMachine.h"


typedef struct _hmmContinuous {
    Hmm baseHmm;
    //double *matchModel;
    //double *extraEventMatchModel;
} HmmContinuous;

typedef struct _strawManHmm {
    HmmContinuous baseContinuousHmm;
    double *transitions;
    double *individualKmerGapProbs; // for learning skip probs/kmer
} ContinuousPairHmm;

typedef struct _vanillaHmm {
    HmmContinuous baseContinuousHmm;
    double *matchModel;
    double *scaledMatchModel;
    double *kmerSkipBins;
    int64_t (*getKmerSkipBin)(double *matchModel, void *cX);
} VanillaHmm;

typedef struct _hdpHmm {
    ContinuousPairHmm baseContinuousPairHmm;
    double threshold;
    void (*addToAssignments)(Hmm *, void *, void *);
    stList *eventAssignments;
    stList *kmerAssignments;
    int64_t numberOfAssignments;
    NanoporeHDP *nhdp;
} HdpHmm;

Hmm *continuousPairHmm_constructEmpty(
        double pseudocount, int64_t stateNumber, int64_t symbolSetSize, StateMachineType type,
        void (*addToTransitionExpFcn)(Hmm *hmm, int64_t from, int64_t to, double p),
        void (*setTransitionFcn)(Hmm *hmm, int64_t from, int64_t to, double p),
        double (*getTransitionsExpFcn)(Hmm *hmm, int64_t from, int64_t to),
        void (*addToKmerGapExpFcn)(Hmm *hmm, int64_t state, int64_t ki, int64_t ignore, double p),
        void (*setKmerGapExpFcn)(Hmm *hmm, int64_t state, int64_t ki, int64_t ignore, double p),
        double (*getKmerGapExpFcn)(Hmm *hmm, int64_t state, int64_t ki, int64_t ignore),
        int64_t (*getElementIndexFcn)(void *));

void continuousPairHmm_addToTransitionsExpectation(Hmm *hmm, int64_t from, int64_t to, double p);

void continuousPairHmm_setTransitionExpectation(Hmm *hmm, int64_t from, int64_t to, double p);

double continuousPairHmm_getTransitionExpectation(Hmm *hmm, int64_t from, int64_t to);

void continuousPairHmm_addToKmerGapExpectation(Hmm *hmm, int64_t state, int64_t kmerIndex, int64_t ignore, double p);

void continuousPairHmm_setKmerGapExpectation(Hmm *hmm, int64_t state, int64_t kmerIndex, int64_t ignore, double p);

double continuousPairHmm_getKmerGapExpectation(Hmm *hmm, int64_t state, int64_t kmerIndex, int64_t ignore);

void continuousPairHmm_loadTransitionsAndKmerGapProbs(StateMachine *sM, Hmm *hmm);

void continuousPairHmm_normalize(Hmm *hmm);

void continuousPairHmm_randomize(Hmm *hmm);

void continuousPairHmm_destruct(Hmm *hmm);

void continuousPairHmm_writeToFile(Hmm *hmm, FILE *fileHandle, double scale, double shift);

Hmm *continuousPairHmm_loadFromFile(const char *fileName);

Hmm *vanillaHmm_constructEmpty(double pseudocount,
                               int64_t stateNumber, int64_t symbolSetSize, StateMachineType type,
                               void (*addToKmerBinExpFcn)(Hmm *hmm, int64_t bin, int64_t ignore, double p),
                               void (*setKmerBinFcn)(Hmm *hmm, int64_t bin, int64_t ignore, double p),
                               double (*getKmerBinExpFcn)(Hmm *hmm, int64_t bin, int64_t ignore));

void vanillaHmm_addToKmerSkipBinExpectation(Hmm *hmm, int64_t bin, int64_t ignore, double p);

void vanillaHmm_setKmerSkipBinExpectation(Hmm *hmm, int64_t bin, int64_t ignore, double p);

double vanillaHmm_getKmerSkipBinExpectation(Hmm *hmm, int64_t bin, int64_t ignore);

void vanillaHmm_normalizeKmerSkipBins(Hmm *hmm);

void vanillaHmm_randomizeKmerSkipBins(Hmm *hmm);

void vanillaHmm_loadKmerSkipBinExpectations(StateMachine *sM, Hmm *hmm);

void vanillaHmm_implantMatchModelsintoHmm(StateMachine *sM, Hmm *hmm);

void vanillaHmm_writeToFile(Hmm *hmm, FILE *fileHandle);

Hmm *vanillaHmm_loadFromFile(const char *fileName);

void vanillaHmm_destruct(Hmm *hmm);

Hmm *hdpHmm_constructEmpty(double pseudocount, int64_t stateNumber, int64_t symbolSetSize, StateMachineType type,
                           double threshold,
                           void (*addToTransitionExpFcn)(Hmm *hmm, int64_t from, int64_t to, double p),
                           void (*setTransitionFcn)(Hmm *hmm, int64_t from, int64_t to, double p),
                           double (*getTransitionsExpFcn)(Hmm *hmm, int64_t from, int64_t to),
                           void (*addToKmerGapExpFcn)(Hmm *hmm, int64_t state, int64_t ki, int64_t ignore, double p),
                           void (*setKmerGapExpFcn)(Hmm *hmm, int64_t state, int64_t ki, int64_t ignore, double p),
                           double (*getKmerGapExpFcn)(Hmm *hmm, int64_t state, int64_t ki, int64_t ignore),
                           int64_t (*getElementIndexFcn)(void *));

void hdpHmm_writeToFile(Hmm *hmm, FILE *fileHandle);

Hmm *hdpHmm_loadFromFile(const char *fileName, NanoporeHDP *ndhp);

void hdpHmm_destruct(Hmm *hmm);

// CORE
Hmm *hmmContinuous_loadSignalHmm(const char *fileName, StateMachineType type);

void hmmContinuous_loadExpectations(StateMachine *sM, Hmm *hmm, StateMachineType type);

void hmmContinuous_destruct(Hmm *hmm, StateMachineType type);

Hmm *hmmContinuous_getEmptyHmm(StateMachineType type, double pseudocount);

void hmmContinuous_normalize(Hmm *hmm, StateMachineType type);

void hmmContinuous_writeToFile(const char *outFile, Hmm *hmm, StateMachineType type);

#endif
