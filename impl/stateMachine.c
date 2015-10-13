/*
 * stateMachine.c
 *
 *  Created on: 1 Aug 2014
 *      Author: benedictpaten
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include "nanopore.h"
#include "bioioC.h"
#include "sonLib.h"
#include "pairwiseAligner.h"
#include "stateMachine.h"
#include "emissionMatrix.h"
#include "discreteHmm.h"

//////////////////////////////////////////////////////////////////////////////
// StateMachine Emission functions for discrete alignments (symbols and kmers)
//////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////// STATIC FUNCTIONS ////////////////////////////////////////////////////////

typedef enum {
    match = 0, shortGapX = 1, shortGapY = 2, longGapX = 3, longGapY = 4
} State;

static void state_check(StateMachine *sM, State s) {
    assert(s >= 0 && s < sM->stateNumber);
}

static void index_check(int64_t c) {
    assert(c >= 0 && c < NUM_OF_KMERS);
}

// from SO: http://stackoverflow.com/questions/213042/how-do-you-do-exponentiation-in-c
static int64_t intPow(int64_t base, int64_t exp) {
    if (exp == 0) {
        return 1;
    } else if (exp % 2) {
        return base * intPow(base, exp - 1);
    } else {
        int64_t tmp = intPow(base, exp / 2);
        return tmp * tmp;
    }
}

static inline void emissions_discrete_initializeEmissionsMatrices(StateMachine *sM) {
    sM->EMISSION_GAP_X_PROBS = st_malloc(sM->parameterSetSize*sizeof(double));
    sM->EMISSION_GAP_Y_PROBS = st_malloc(sM->parameterSetSize*sizeof(double));
    sM->EMISSION_MATCH_PROBS = st_malloc(sM->parameterSetSize*sM->parameterSetSize*sizeof(double));
}

void emissions_symbol_setEmissionsToDefaults(StateMachine *sM) {
    // initialize
    emissions_discrete_initializeEmissionsMatrices(sM);

    // Set Match probs to default values
    const double EMISSION_MATCH=-2.1149196655034745; //log(0.12064298095701059);
    const double EMISSION_TRANSVERSION=-4.5691014376830479; //log(0.010367271172731285);
    const double EMISSION_TRANSITION=-3.9833860032220842; //log(0.01862247669752685);

    const double M[SYMBOL_NUMBER_NO_N*SYMBOL_NUMBER_NO_N] = {
            EMISSION_MATCH, EMISSION_TRANSVERSION, EMISSION_TRANSITION, EMISSION_TRANSVERSION,
            EMISSION_TRANSVERSION, EMISSION_MATCH, EMISSION_TRANSVERSION, EMISSION_TRANSITION,
            EMISSION_TRANSITION, EMISSION_TRANSVERSION, EMISSION_MATCH, EMISSION_TRANSVERSION,
            EMISSION_TRANSVERSION, EMISSION_TRANSITION, EMISSION_TRANSVERSION, EMISSION_MATCH };

    memcpy(sM->EMISSION_MATCH_PROBS, M, sizeof(double)*SYMBOL_NUMBER_NO_N*SYMBOL_NUMBER_NO_N);

    // Set Gap probs to default values
    const double EMISSION_GAP = -1.6094379124341003; //log(0.2)
    const double G[4] = { EMISSION_GAP, EMISSION_GAP, EMISSION_GAP, EMISSION_GAP };
    memcpy(sM->EMISSION_GAP_X_PROBS, G, sizeof(double)*SYMBOL_NUMBER_NO_N);
    memcpy(sM->EMISSION_GAP_Y_PROBS, G, sizeof(double)*SYMBOL_NUMBER_NO_N);
}

static inline void emissions_discrete_initMatchProbsToZero(double *emissionMatchProbs, int64_t symbolSetSize) {
    memset(emissionMatchProbs, 0, symbolSetSize*symbolSetSize*sizeof(double));
}

static inline void emissions_discrete_initGapProbsToZero(double *emissionGapProbs, int64_t symbolSetSize) {
    memset(emissionGapProbs, 0, symbolSetSize*sizeof(double));
}

///////////////////////////////////////////// CORE FUNCTIONS ////////////////////////////////////////////////////////

void emissions_discrete_initEmissionsToZero(StateMachine *sM) {
    // initialize
    emissions_discrete_initializeEmissionsMatrices(sM);
    // set match matrix to zeros
    emissions_discrete_initMatchProbsToZero(sM->EMISSION_MATCH_PROBS, sM->parameterSetSize);
    // set gap matrix to zeros
    emissions_discrete_initGapProbsToZero(sM->EMISSION_GAP_X_PROBS, sM->parameterSetSize);
    emissions_discrete_initGapProbsToZero(sM->EMISSION_GAP_Y_PROBS, sM->parameterSetSize);
}

int64_t emissions_discrete_getBaseIndex(void *base) {
    char b = *(char*) base;
    switch (b) {
        case 'A':
            return 0;
        case 'C':
            return 1;
        case 'G':
            return 2;
        case 'T':
            return 3;
        default: // N
            return 4;
    }
}

int64_t emissions_discrete_getKmerIndex(void *kmer) {
    int64_t kmerLen = strlen((char*) kmer);
    if (kmerLen == 0) {
        return NUM_OF_KMERS+1;
    }
    int64_t axisLength = intPow(SYMBOL_NUMBER_NO_N, KMER_LENGTH);
    int64_t l = axisLength / SYMBOL_NUMBER_NO_N;
    int64_t i = 0;
    int64_t x = 0;
    while(l > 1) {
        x += l * emissions_discrete_getBaseIndex(kmer + i);
        i += 1;
        l = l/SYMBOL_NUMBER_NO_N;
    }
    int64_t last = strlen(kmer)-1;
    x += emissions_discrete_getBaseIndex(kmer + last);
    return x;
}

double emissions_symbol_getGapProb(const double *emissionGapProbs, void *base) {
    int64_t i = emissions_discrete_getBaseIndex(base);
    index_check(i);
    if(i == 4) {
        return -1.386294361; //log(0.25)
    }
    return emissionGapProbs[i];
}

double emissions_symbol_getMatchProb(const double *emissionMatchProbs, void *x, void *y) {
    int64_t iX = emissions_discrete_getBaseIndex(x);
    int64_t iY = emissions_discrete_getBaseIndex(y);
    index_check(iX);
    index_check(iY);
    if(iX == 4 || iY == 4) {
        return -2.772588722; //log(0.25**2)
    }
    return emissionMatchProbs[iX * SYMBOL_NUMBER_NO_N + iY];
}

double emissions_kmer_getGapProb(const double *emissionGapProbs, void *kmer) {
    int64_t i = emissions_discrete_getKmerIndex(kmer);
    index_check(i);
    return emissionGapProbs[i];
}

double emissions_kmer_getMatchProb(const double *emissionMatchProbs, void *x, void *y) {
    int64_t iX = emissions_discrete_getKmerIndex(x);
    int64_t iY = emissions_discrete_getKmerIndex(y);
    int64_t tableIndex = iX * NUM_OF_KMERS + iY;
    return emissionMatchProbs[tableIndex];
}
/////////////////////////////////////////
// functions for signal/kmer alignment //
/////////////////////////////////////////

/////////////////////////////////////////// STATIC FUNCTIONS ////////////////////////////////////////////////////////

static inline void emissions_signal_initializeEmissionsMatrices(StateMachine *sM) {
    // changed to 30 for skip prob bins
    sM->EMISSION_GAP_X_PROBS = st_malloc(30 * sizeof(double));
    sM->EMISSION_GAP_Y_PROBS = st_malloc(1 + (sM->parameterSetSize * MODEL_PARAMS) * sizeof(double));
    sM->EMISSION_MATCH_PROBS = st_malloc(1 + (sM->parameterSetSize * MODEL_PARAMS) * sizeof(double));
}

static inline void emissions_signal_initMatchMatrixToZero(double *matchModel, int64_t parameterSetSize) {
    memset(matchModel, 0, (1 + (parameterSetSize * MODEL_PARAMS)) * sizeof(double));
}

static inline void emissions_signal_initKmerSkipTableToZero(double *skipModel, int64_t parameterSetSize) {
    memset(skipModel, 0, parameterSetSize * sizeof(double));
}

static inline double emissions_signal_getModelLevelMean(const double *eventModel, int64_t kmerIndex) {
    // 1 + i*MODEL PARAMS because the first element is the correlation parameter
    return kmerIndex > NUM_OF_KMERS ? 0.0 : eventModel[1 + (kmerIndex * MODEL_PARAMS)];
}

static inline double emissions_signal_getModelLevelSd(const double *eventModel, int64_t kmerIndex) {
    return kmerIndex > NUM_OF_KMERS ? 0.0 : eventModel[1 + (kmerIndex * MODEL_PARAMS + 1)];
}

static inline double emissions_signal_getModelFluctuationMean(const double *eventModel, int64_t kmerIndex) {
    return kmerIndex > NUM_OF_KMERS ? 0.0 : eventModel[1 + (kmerIndex * MODEL_PARAMS + 2)];
}

static inline double emissions_signal_getModelFluctuationSd(const double *eventModel, int64_t kmerIndex) {
    return kmerIndex > NUM_OF_KMERS ? 0.0 : eventModel[1 + (kmerIndex * MODEL_PARAMS + 3)];
}

static void emissions_signal_loadPoreModel(StateMachine *sM, const char *modelFile) {
    /*
     *  the model file has the format:
     *  line 1: [correlation coefficient] [level_mean] [level_sd] [noise_mean] [noise_sd] (.../kmer) \n
     *  line 2: skip bins \n
     *  line 3: [correlation coefficient] [level_mean] [level_sd, scaled] [noise_mean] [noise_sd] (.../kmer) \n
     */

    FILE *fH = fopen(modelFile, "r");

    // Line 1: parse the match emissions line
    char *string = stFile_getLineFromFile(fH);
    stList *tokens = stString_split(string);
    // check to make sure that the model will fit in the stateMachine
    if (stList_length(tokens) != 1 + (sM->parameterSetSize * MODEL_PARAMS)) {
        st_errAbort("This stateMachine is not correct for signal model (match emissions)\n");
    }
    // load the model into the state machine emissions
    for (int64_t i = 0; i < 1 + (sM->parameterSetSize * MODEL_PARAMS); i++) {
        int64_t j = sscanf(stList_get(tokens, i), "%lf", &(sM->EMISSION_MATCH_PROBS[i]));
        if (j != 1) {
            st_errAbort("emissions_signal_loadPoreModel: error loading pore model (match emissions)\n");
        }
    }
    // clean up match emissions line
    free(string);
    stList_destruct(tokens);

    // Line 2: parse X Gap emissions line
    string = stFile_getLineFromFile(fH);
    tokens = stString_split(string);
    // check for correctness
    if (stList_length(tokens) != 30) {
        st_errAbort("Did not get expected number of kmer skip bins, expected 30, got %lld\n", stList_length(tokens));
    }
    // load X Gap emissions into stateMachine
    for (int64_t i = 0; i < 30; i++) {
        int64_t j = sscanf(stList_get(tokens, i), "%lf", &(sM->EMISSION_GAP_X_PROBS[i]));
        if (j != 1) {
            st_errAbort("emissions_signal_loadPoreModel: error loading kmer skip bins\n");
        }
    }
    // clean up X Gap emissions line
    free(string);
    stList_destruct(tokens);

    // parse Y Gap emissions line
    string = stFile_getLineFromFile(fH);
    tokens = stString_split(string);
    // check to make sure that the model will fit in the stateMachine
    if (stList_length(tokens) != 1 + (sM->parameterSetSize * MODEL_PARAMS)) {
        st_errAbort("This stateMachine is not correct for signal model (dupeEvent - Y emissions)\n");
    }
    // load the model into the state machine emissions
    for (int64_t i = 0; i < 1 + (sM->parameterSetSize * MODEL_PARAMS); i++) {
        int64_t j = sscanf(stList_get(tokens, i), "%lf", &(sM->EMISSION_GAP_Y_PROBS[i]));
        if (j != 1) {
            st_errAbort("emissions_signal_loadPoreModel: error loading pore model (dupeEvent - Y emissions)\n");
        }
    }
    // clean up match emissions line
    free(string);
    stList_destruct(tokens);

    // close file
    fclose(fH);
}

///////////////////////////////////////////// CORE FUNCTIONS ////////////////////////////////////////////////////////

void emissions_signal_initEmissionsToZero(StateMachine *sM) {
    // initialize
    emissions_signal_initializeEmissionsMatrices(sM);
    // set gap matrices to zeros
    emissions_signal_initKmerSkipTableToZero(sM->EMISSION_GAP_X_PROBS, 30);
    emissions_signal_initMatchMatrixToZero(sM->EMISSION_GAP_Y_PROBS, sM->parameterSetSize);
    // set match matrix to zeros
    emissions_signal_initMatchMatrixToZero(sM->EMISSION_MATCH_PROBS, sM->parameterSetSize);
}

double emissions_signal_getKmerSkipProb(StateMachine *sM, void *kmers) {
    StateMachine3Vanilla *sM3v = (StateMachine3Vanilla *) sM;
    // make kmer_i-1
    char *kmer_im1 = malloc((KMER_LENGTH) * sizeof(char));
    for (int64_t x = 0; x < KMER_LENGTH; x++) {
        kmer_im1[x] = *((char *)kmers+x);
    }
    kmer_im1[KMER_LENGTH] = '\0';

    // make kmer_i
    char *kmer_i = malloc((KMER_LENGTH) * sizeof(char));
    for (int64_t x = 0; x < KMER_LENGTH; x++) {
        kmer_i[x] = *((char *)kmers+(x+1));
    }
    kmer_i[KMER_LENGTH] = '\0';

    // get indices
    int64_t k_i= emissions_discrete_getKmerIndex(kmer_i);
    int64_t k_im1 = emissions_discrete_getKmerIndex(kmer_im1);

    // get the expected mean current for each one
    double u_ki = emissions_signal_getModelLevelMean(sM3v->model.EMISSION_MATCH_PROBS, k_i);
    double u_kim1 = emissions_signal_getModelLevelMean(sM3v->model.EMISSION_MATCH_PROBS, k_im1);

    // find the difference
    double d = fabs(u_ki - u_kim1);

    // get the 'bin' for skip prob, clamp to the last bin
    int64_t bin = (int64_t)(d / 0.5); // 0.5 pA bins right now
    bin = bin >= 30 ? 29 : bin;

    // for debugging
    //double pSkip = sM3v->model.EMISSION_GAP_X_PROBS[bin];
    //st_uglyf("SENTINAL - getKmerSkipProb; kmer_i: %s (index: %lld, model u=%f), kmer_im1: %s (index: %lld, model u=%f),\n d: %f, bin: %lld, pSkip: %f\n",
    //         kmer_i, k_i, u_ki, kmer_im1, k_im1, u_kim1, d, bin, pSkip);

    // clean up
    free(kmer_im1);
    free(kmer_i);
    return sM3v->model.EMISSION_GAP_X_PROBS[bin];
}

double emissions_signal_getlogGaussPDFMatchProb(const double *eventModel, void *kmer, void *event) {
    // make temp kmer
    char *kmer_i = malloc((KMER_LENGTH) * sizeof(char));
    for (int64_t x = 0; x < KMER_LENGTH; x++) {
        kmer_i[x] = *((char *)kmer+(x+1));
    }
    kmer_i[KMER_LENGTH] = '\0';

    // get event mean, and kmer index
    double eventMean = *(double *) event;
    int64_t kmerIndex = emissions_discrete_getKmerIndex(kmer_i);
    double log_inv_sqrt_2pi = log(0.3989422804014327); // constant
    // 1 + because the first element in the array is the covariance of mean and fluctuation
    //double modelMean = eventModel[1 + (kmerIndex * MODEL_PARAMS)];
    double modelMean = emissions_signal_getModelLevelMean(eventModel, kmerIndex);
    //double modelStdDev = eventModel[1 + (kmerIndex * MODEL_PARAMS + 1)];
    double modelStdDev = emissions_signal_getModelLevelSd(eventModel, kmerIndex);
    double log_modelSD = log(modelStdDev);
    double a = (eventMean - modelMean) / modelStdDev;
    // clean up
    free(kmer_i);

    /// / debugging
    //double prob = log_inv_sqrt_2pi - log_modelSD + (-0.5f * a * a);
    //st_uglyf("MATCHING--kmer:%s (index: %lld), event mean: %f, levelMean: %f, prob: %f\n", kmer_i, kmerIndex, eventMean, modelMean, prob);

    return log_inv_sqrt_2pi - log_modelSD + (-0.5f * a * a);
}

double emissions_signal_getBivariateGaussPdfMatchProb(const double *eventModel, void *kmer, void *event) {
    // wrangle event data
    double eventMean = *(double *) event;
    double eventNoise = *(double *) (event + sizeof(double)); // aaah pointers
    // correlation coefficient is the 0th member of the event model
    double p = eventModel[0];
    double pSq = p * p;
    // get model parameters
    int64_t kmerIndex = emissions_discrete_getKmerIndex(kmer);

    //double levelMean = eventModel[1 + (kmerIndex * MODEL_PARAMS)];
    double levelMean = emissions_signal_getModelLevelMean(eventModel, kmerIndex);
    //double levelStdDev = eventModel[1 + (kmerIndex * MODEL_PARAMS + 1)];
    double levelStdDev = emissions_signal_getModelLevelSd(eventModel, kmerIndex);
    //double noiseMean = eventModel[1 + (kmerIndex * MODEL_PARAMS + 2)];
    double noiseMean = emissions_signal_getModelFluctuationMean(eventModel, kmerIndex);
    //double noiseStdDev = eventModel[1 + (kmerIndex * MODEL_PARAMS + 3)];
    double noiseStdDev = emissions_signal_getModelFluctuationSd(eventModel, kmerIndex);

    // do calculation
    double log_inv_2pi = -1.8378770664093453;
    double expC = -1 / (2 * (1 - pSq));
    double xu = (eventMean - levelMean) / levelStdDev;
    double yu = (eventNoise - noiseMean) / noiseStdDev;
    double a = expC * ((xu * xu) + (yu * yu) - (2 * p * xu * yu));
    double c = log_inv_2pi - log(levelStdDev * noiseStdDev * sqrt(1 - pSq));
    return c + a;
}

void emissions_signal_scaleModel(StateMachine *sM,
                                 double scale, double shift, double var,
                                 double scale_sd, double var_sd) {
    // model is arranged: level_mean, level_stdev, sd_mean, sd_stdev per kmer
    // already been adjusted for correlation coeff.
    for (int64_t i = 1; i < (sM->parameterSetSize * MODEL_PARAMS) + 1; i += 4) {
        sM->EMISSION_MATCH_PROBS[i] = sM->EMISSION_MATCH_PROBS[i] * scale + shift; // mean = mean * scale + shift
        sM->EMISSION_MATCH_PROBS[i+1] = sM->EMISSION_MATCH_PROBS[i+1] * var; // stdev = stdev * var
        sM->EMISSION_MATCH_PROBS[i+2] = sM->EMISSION_MATCH_PROBS[i+2] * scale_sd;
        sM->EMISSION_MATCH_PROBS[i+3] = sM->EMISSION_MATCH_PROBS[i+3] * sqrt(pow(scale_sd, 3.0) / var_sd);
    }
}


////////////////////////////
// EM emissions functions //
////////////////////////////


static void emissions_em_loadMatchProbs(double *emissionMatchProbs, Hmm *hmm, int64_t matchState) {
    //Load the matches
    for(int64_t x = 0; x < hmm->symbolSetSize; x++) {
        for(int64_t y = 0; y < hmm->symbolSetSize; y++) {
            emissionMatchProbs[x * hmm->symbolSetSize + y] = log(hmm->getEmissionExpFcn(hmm, matchState, x, y));
        }
    }
}

static void emissions_em_loadMatchProbsSymmetrically(double *emissionMatchProbs, Hmm *hmm, int64_t matchState) {
    //Load the matches
    for(int64_t x = 0; x < hmm->symbolSetSize; x++) {
        emissionMatchProbs[x * hmm->symbolSetSize + x] = log(hmm->getEmissionExpFcn(hmm, matchState, x, x));
        for(int64_t y=x+1; y<hmm->symbolSetSize; y++) {
            double d = log((hmm->getEmissionExpFcn(hmm, matchState, x, y) +
                    hmm->getEmissionExpFcn(hmm, matchState, y, x)) / 2.0);
            emissionMatchProbs[x * hmm->symbolSetSize + y] = d;
            emissionMatchProbs[y * hmm->symbolSetSize + x] = d;
        }
    }
}

static void emissions_em_collapseMatrixEmissions(Hmm *hmm, int64_t state, double *gapEmissions, bool collapseToX) {
    for(int64_t x=0; x<hmm->symbolSetSize; x++) {
        for(int64_t y=0; y<hmm->symbolSetSize; y++) {
            gapEmissions[collapseToX ? x : y] += hmm->getEmissionExpFcn(hmm, state, x, y);
        }
    }
}


static void emissions_em_loadGapProbs(double *emissionGapProbs, Hmm *hmm,
                                      int64_t *xGapStates, int64_t xGapStateNo,
                                      int64_t *yGapStates, int64_t yGapStateNo) {
    //Initialise to 0.0
    for(int64_t i=0; i < hmm->symbolSetSize; i++) {
        emissionGapProbs[i] = 0.0;
    }
    //Load the probs taking the average over all the gap states
    for(int64_t i=0; i < xGapStateNo; i++) {
        emissions_em_collapseMatrixEmissions(hmm, xGapStates[i], emissionGapProbs, 1);
    }
    for(int64_t i=0; i<yGapStateNo; i++) {
        emissions_em_collapseMatrixEmissions(hmm, yGapStates[i], emissionGapProbs, 0);
    }
    //Now normalise
    double total = 0.0;
    for(int64_t i=0; i < hmm->symbolSetSize; i++) {
        total += emissionGapProbs[i];
    }
    for(int64_t i=0; i< hmm->symbolSetSize; i++) {
        emissionGapProbs[i] = log(emissionGapProbs[i]/total);
    }
}


///////////////////////////////////
///////////////////////////////////
//Five state state-machine
///////////////////////////////////
///////////////////////////////////

/////////////////////////////////////////// STATIC FUNCTIONS ////////////////////////////////////////////////////////

static double stateMachine5_startStateProb(StateMachine *sM, int64_t state) {
    //Match state is like going to a match.
    state_check(sM, state);
    return state == match ? 0 : LOG_ZERO;
}

static double stateMachine5_raggedStartStateProb(StateMachine *sM, int64_t state) {
    state_check(sM, state);
    return (state == longGapX || state == longGapY) ? 0 : LOG_ZERO;
}

static double stateMachine5_endStateProb(StateMachine *sM, int64_t state) {
    //End state is like to going to a match
    StateMachine5 *sM5 = (StateMachine5 *) sM;
    state_check(sM, state);
    switch (state) {
    case match:
        return sM5->TRANSITION_MATCH_CONTINUE;
    case shortGapX:
        return sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X;
    case shortGapY:
        return sM5->TRANSITION_MATCH_FROM_SHORT_GAP_Y;
    case longGapX:
        return sM5->TRANSITION_MATCH_FROM_LONG_GAP_X;
    case longGapY:
        return sM5->TRANSITION_MATCH_FROM_LONG_GAP_Y;
    }
    return 0.0;
}

static double stateMachine5_raggedEndStateProb(StateMachine *sM, int64_t state) {
    //End state is like to going to a match
    StateMachine5 *sM5 = (StateMachine5 *) sM;
    state_check(sM, state);
    switch (state) {
    case match:
        return sM5->TRANSITION_GAP_LONG_OPEN_X;
    case shortGapX:
        return sM5->TRANSITION_GAP_LONG_OPEN_X;
    case shortGapY:
        return sM5->TRANSITION_GAP_LONG_OPEN_Y;
    case longGapX:
        return sM5->TRANSITION_GAP_LONG_EXTEND_X;
    case longGapY:
        return sM5->TRANSITION_GAP_LONG_EXTEND_Y;
    }
    return 0.0;
}

static void stateMachine5_cellCalculate(StateMachine *sM,
                                        double *current, double *lower, double *middle, double *upper,
                                        void *cX, void *cY,
                                        void (*doTransition)(double *, double *, // fromCells, toCells
                                                             int64_t, int64_t,   // from, to
                                                             double, double,     // emissionProb, transitionProb
                                                             void *),            // extraArgs
                                        void *extraArgs) {
    StateMachine5 *sM5 = (StateMachine5 *) sM;
    if (lower != NULL) {
        double eP = sM5->getXGapProbFcn(sM5->model.EMISSION_GAP_X_PROBS, cX);
        doTransition(lower, current, match, shortGapX, eP, sM5->TRANSITION_GAP_SHORT_OPEN_X, extraArgs);
        doTransition(lower, current, shortGapX, shortGapX, eP, sM5->TRANSITION_GAP_SHORT_EXTEND_X, extraArgs);
        // how come these are commented out?
        //doTransition(lower, current, shortGapY, shortGapX, eP, sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X, extraArgs);
        doTransition(lower, current, match, longGapX, eP, sM5->TRANSITION_GAP_LONG_OPEN_X, extraArgs);
        doTransition(lower, current, longGapX, longGapX, eP, sM5->TRANSITION_GAP_LONG_EXTEND_X, extraArgs);
        //doTransition(lower, current, longGapY, longGapX, eP, sM5->TRANSITION_GAP_LONG_SWITCH_TO_X, extraArgs);
    }
    if (middle != NULL) {
        double eP = sM5->getMatchProbFcn(sM5->model.EMISSION_MATCH_PROBS, cX, cY);
        doTransition(middle, current, match, match, eP, sM5->TRANSITION_MATCH_CONTINUE, extraArgs);
        doTransition(middle, current, shortGapX, match, eP, sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X, extraArgs);
        doTransition(middle, current, shortGapY, match, eP, sM5->TRANSITION_MATCH_FROM_SHORT_GAP_Y, extraArgs);
        doTransition(middle, current, longGapX, match, eP, sM5->TRANSITION_MATCH_FROM_LONG_GAP_X, extraArgs);
        doTransition(middle, current, longGapY, match, eP, sM5->TRANSITION_MATCH_FROM_LONG_GAP_Y, extraArgs);
    }
    if (upper != NULL) {
        double eP = sM5->getYGapProbFcn(sM5->model.EMISSION_GAP_Y_PROBS, cY);
        doTransition(upper, current, match, shortGapY, eP, sM5->TRANSITION_GAP_SHORT_OPEN_Y, extraArgs);
        doTransition(upper, current, shortGapY, shortGapY, eP, sM5->TRANSITION_GAP_SHORT_EXTEND_Y, extraArgs);
        //doTransition(upper, current, shortGapX, shortGapY, eP, sM5->TRANSITION_GAP_SHORT_SWITCH_TO_Y, extraArgs);
        doTransition(upper, current, match, longGapY, eP, sM5->TRANSITION_GAP_LONG_OPEN_Y, extraArgs);
        doTransition(upper, current, longGapY, longGapY, eP, sM5->TRANSITION_GAP_LONG_EXTEND_Y, extraArgs);
        //doTransition(upper, current, longGapX, longGapY, eP, sM5->TRANSITION_GAP_LONG_SWITCH_TO_Y, extraArgs);
    }
}

///////////////////////////////////////////// CORE FUNCTIONS ////////////////////////////////////////////////////////

StateMachine *stateMachine5_construct(StateMachineType type, int64_t parameterSetSize,
                                      void (*setEmissionsDefaults)(StateMachine *sM),
                                      double (*gapXProbFcn)(const double *, void *),
                                      double (*gapYProbFcn)(const double *, void *),
                                      double (*matchProbFcn)(const double *, void *, void *)) {
    /*
     * Description of (potentially ambigious) arguments:
     * parameterSetSize = the number of kmers that we are using, of len(kmer) = 1, then the number is 4 (or 5 if we're
     * including N). It's 25 if len(kmer) = 2, it's 4096 in the 6-mer model.
     *
     */
    StateMachine5 *sM5 = st_malloc(sizeof(StateMachine5));
    if(type != fiveState && type != fiveStateAsymmetric) {
        st_errAbort("Wrong type for five state %i", type);
    }
    // setup transitions, specific to stateMachine5
    sM5->TRANSITION_MATCH_CONTINUE = -0.030064059121770816; //0.9703833696510062f
    sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X = -1.272871422049609; //1.0 - gapExtend - gapSwitch = 0.280026392297485
    sM5->TRANSITION_MATCH_FROM_LONG_GAP_X = -5.673280173170473; //1.0 - gapExtend = 0.00343657420938
    sM5->TRANSITION_GAP_SHORT_OPEN_X = -4.34381910900448; //0.0129868352330243
    sM5->TRANSITION_GAP_SHORT_EXTEND_X = -0.3388262689231553; //0.7126062401851738f;
    sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X = -4.910694825551255; //0.0073673675173412815f;
    sM5->TRANSITION_GAP_LONG_OPEN_X = -6.30810595366929; //(1.0 - match - 2*gapOpenShort)/2 = 0.001821479941473
    sM5->TRANSITION_GAP_LONG_EXTEND_X = -0.003442492794189331; //0.99656342579062f;
    sM5->TRANSITION_GAP_LONG_SWITCH_TO_X = -6.30810595366929; //0.99656342579062f;
    // make it symmetric
    sM5->TRANSITION_MATCH_FROM_SHORT_GAP_Y = sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X;
    sM5->TRANSITION_MATCH_FROM_LONG_GAP_Y = sM5->TRANSITION_MATCH_FROM_LONG_GAP_X;
    sM5->TRANSITION_GAP_SHORT_OPEN_Y = sM5->TRANSITION_GAP_SHORT_OPEN_X;
    sM5->TRANSITION_GAP_SHORT_EXTEND_Y = sM5->TRANSITION_GAP_SHORT_EXTEND_X;
    sM5->TRANSITION_GAP_SHORT_SWITCH_TO_Y = sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X;
    sM5->TRANSITION_GAP_LONG_OPEN_Y = sM5->TRANSITION_GAP_LONG_OPEN_X;
    sM5->TRANSITION_GAP_LONG_EXTEND_Y = sM5->TRANSITION_GAP_LONG_EXTEND_X;
    sM5->TRANSITION_GAP_LONG_SWITCH_TO_Y = sM5->TRANSITION_GAP_LONG_SWITCH_TO_X;
    // setup the parent class
    sM5->model.type = type;
    sM5->model.parameterSetSize = parameterSetSize;
    sM5->model.stateNumber = 5;
    sM5->model.matchState = match;
    sM5->model.startStateProb = stateMachine5_startStateProb;
    sM5->model.endStateProb = stateMachine5_endStateProb;
    sM5->model.raggedStartStateProb = stateMachine5_raggedStartStateProb;
    sM5->model.raggedEndStateProb = stateMachine5_raggedEndStateProb;
    sM5->getXGapProbFcn = gapXProbFcn;
    sM5->getYGapProbFcn = gapYProbFcn;
    sM5->getMatchProbFcn = matchProbFcn;
    sM5->model.cellCalculate = stateMachine5_cellCalculate;
    // set emissions to defaults (or zeros)
    setEmissionsDefaults((StateMachine *) sM5);

    return (StateMachine *) sM5;
}

static void switchDoubles(double *a, double *b) {
    double c = *a;
    *a = *b;
    *b = c;
}

///////////////////////////////
// EM - StateMachine5 functions/
///////////////////////////////

static void stateMachine5_loadAsymmetric(StateMachine5 *sM5, Hmm *hmm) {
    if (hmm->type != fiveStateAsymmetric) {
        st_errAbort("Wrong hmm type");
    }
    sM5->TRANSITION_MATCH_CONTINUE = log(hmm->getTransitionsExpFcn(hmm, match, match)); //0.9703833696510062f

    sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X = log(hmm->getTransitionsExpFcn(hmm, shortGapX, match));
    sM5->TRANSITION_MATCH_FROM_LONG_GAP_X = log(hmm->getTransitionsExpFcn(hmm, longGapX, match));
    sM5->TRANSITION_GAP_SHORT_OPEN_X = log(hmm->getTransitionsExpFcn(hmm, match, shortGapX));
    sM5->TRANSITION_GAP_SHORT_EXTEND_X = log(hmm->getTransitionsExpFcn(hmm, shortGapX, shortGapX));
    sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X = log(hmm->getTransitionsExpFcn(hmm, shortGapY, shortGapX));
    sM5->TRANSITION_GAP_LONG_OPEN_X = log(hmm->getTransitionsExpFcn(hmm, match, longGapX));
    sM5->TRANSITION_GAP_LONG_EXTEND_X = log(hmm->getTransitionsExpFcn(hmm, longGapX, longGapX));
    sM5->TRANSITION_GAP_LONG_SWITCH_TO_X = log(hmm->getTransitionsExpFcn(hmm, longGapY, longGapX));

    if(sM5->TRANSITION_GAP_SHORT_EXTEND_X > sM5->TRANSITION_GAP_LONG_EXTEND_X) {
        // Switch the long and short gap parameters if one the "long states" have a smaller
        // extend probability than the "short states", as can randomly happen during EM training.
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_EXTEND_X), &(sM5->TRANSITION_GAP_LONG_EXTEND_X));
        switchDoubles(&(sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X), &(sM5->TRANSITION_MATCH_FROM_LONG_GAP_X));
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_OPEN_X), &(sM5->TRANSITION_GAP_LONG_OPEN_X));
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X), &(sM5->TRANSITION_GAP_LONG_SWITCH_TO_X));
    }

    sM5->TRANSITION_MATCH_FROM_SHORT_GAP_Y = log(hmm->getTransitionsExpFcn(hmm, shortGapY, match));
    sM5->TRANSITION_MATCH_FROM_LONG_GAP_Y = log(hmm->getTransitionsExpFcn(hmm, longGapY, match));
    sM5->TRANSITION_GAP_SHORT_OPEN_Y = log(hmm->getTransitionsExpFcn(hmm, match, shortGapY));
    sM5->TRANSITION_GAP_SHORT_EXTEND_Y = log(hmm->getTransitionsExpFcn(hmm, shortGapY, shortGapY));
    sM5->TRANSITION_GAP_SHORT_SWITCH_TO_Y = log(hmm->getTransitionsExpFcn(hmm, shortGapX, shortGapY));
    sM5->TRANSITION_GAP_LONG_OPEN_Y = log(hmm->getTransitionsExpFcn(hmm, match, longGapY));
    sM5->TRANSITION_GAP_LONG_EXTEND_Y = log(hmm->getTransitionsExpFcn(hmm, longGapY, longGapY));
    sM5->TRANSITION_GAP_LONG_SWITCH_TO_Y = log(hmm->getTransitionsExpFcn(hmm, longGapX, longGapY));

    if(sM5->TRANSITION_GAP_SHORT_EXTEND_Y > sM5->TRANSITION_GAP_LONG_EXTEND_Y) {
        // Switch the long and short gap parameters if one the "long states" have a smaller
        // extend probability than the "short states", as can randomly happen during EM training.
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_EXTEND_Y), &(sM5->TRANSITION_GAP_LONG_EXTEND_Y));
        switchDoubles(&(sM5->TRANSITION_MATCH_FROM_SHORT_GAP_Y), &(sM5->TRANSITION_MATCH_FROM_LONG_GAP_Y));
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_OPEN_Y), &(sM5->TRANSITION_GAP_LONG_OPEN_Y));
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_SWITCH_TO_Y), &(sM5->TRANSITION_GAP_LONG_SWITCH_TO_Y));
    }

    emissions_em_loadMatchProbs(sM5->model.EMISSION_MATCH_PROBS, hmm, match);
    int64_t xGapStates[2] = { shortGapX, longGapX };
    int64_t yGapStates[2] = { shortGapY, longGapY };
    emissions_em_loadGapProbs(sM5->model.EMISSION_GAP_X_PROBS, hmm, xGapStates, 2, NULL, 0);
    emissions_em_loadGapProbs(sM5->model.EMISSION_GAP_Y_PROBS, hmm, NULL, 0, yGapStates, 2);
}

static void stateMachine5_loadSymmetric(StateMachine5 *sM5, Hmm *hmm) {
    if (hmm->type != fiveState) {
        printf("Wrong hmm type");
        st_errAbort("Wrong hmm type");
    }

    sM5->TRANSITION_MATCH_CONTINUE = log(hmm->getTransitionsExpFcn(hmm, match, match));
    sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X = log(
            (hmm->getTransitionsExpFcn(hmm, shortGapX, match) +
             hmm->getTransitionsExpFcn(hmm, shortGapY, match)) / 2); //1.0 - gapExtend - gapSwitch = 0.280026392297485
    sM5->TRANSITION_MATCH_FROM_LONG_GAP_X = log(
            (hmm->getTransitionsExpFcn(hmm, longGapX, match) +
             hmm->getTransitionsExpFcn(hmm, longGapY, match)) / 2); //1.0 - gapExtend = 0.00343657420938
    sM5->TRANSITION_GAP_SHORT_OPEN_X = log(
            (hmm->getTransitionsExpFcn(hmm, match, shortGapX) +
             hmm->getTransitionsExpFcn(hmm, match, shortGapY)) / 2); //0.0129868352330243
    sM5->TRANSITION_GAP_SHORT_EXTEND_X = log(
            (hmm->getTransitionsExpFcn(hmm, shortGapX, shortGapX) +
             hmm->getTransitionsExpFcn(hmm, shortGapY, shortGapY)) / 2); //0.7126062401851738f;
    sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X = log(
            (hmm->getTransitionsExpFcn(hmm, shortGapX, shortGapY) +
             hmm->getTransitionsExpFcn(hmm, shortGapY, shortGapX)) / 2); //0.0073673675173412815f;
    sM5->TRANSITION_GAP_LONG_OPEN_X = log(
            (hmm->getTransitionsExpFcn(hmm, match, longGapX) +
             hmm->getTransitionsExpFcn(hmm, match, longGapY)) / 2); //(1.0 - match - 2*gapOpenShort)/2 = 0.001821479941473
    sM5->TRANSITION_GAP_LONG_EXTEND_X = log(
            (hmm->getTransitionsExpFcn(hmm, longGapX, longGapX) +
             hmm->getTransitionsExpFcn(hmm, longGapY, longGapY)) / 2);
    sM5->TRANSITION_GAP_LONG_SWITCH_TO_X = log(
            (hmm->getTransitionsExpFcn(hmm, longGapX, longGapY) +
             hmm->getTransitionsExpFcn(hmm, longGapY, longGapX)) / 2); //0.0073673675173412815f;

    if(sM5->TRANSITION_GAP_SHORT_EXTEND_X > sM5->TRANSITION_GAP_LONG_EXTEND_X) {
        //Switch the long and short gap parameters if one the "long states" have a smaller extend probability than the "short states", as can randomly happen during EM training.
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_EXTEND_X), &(sM5->TRANSITION_GAP_LONG_EXTEND_X));
        switchDoubles(&(sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X), &(sM5->TRANSITION_MATCH_FROM_LONG_GAP_X));
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_OPEN_X), &(sM5->TRANSITION_GAP_LONG_OPEN_X));
        switchDoubles(&(sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X), &(sM5->TRANSITION_GAP_LONG_SWITCH_TO_X));
    }

    sM5->TRANSITION_MATCH_FROM_SHORT_GAP_Y = sM5->TRANSITION_MATCH_FROM_SHORT_GAP_X;
    sM5->TRANSITION_MATCH_FROM_LONG_GAP_Y = sM5->TRANSITION_MATCH_FROM_LONG_GAP_X;
    sM5->TRANSITION_GAP_SHORT_OPEN_Y = sM5->TRANSITION_GAP_SHORT_OPEN_X;
    sM5->TRANSITION_GAP_SHORT_EXTEND_Y = sM5->TRANSITION_GAP_SHORT_EXTEND_X;
    sM5->TRANSITION_GAP_SHORT_SWITCH_TO_Y = sM5->TRANSITION_GAP_SHORT_SWITCH_TO_X;
    sM5->TRANSITION_GAP_LONG_OPEN_Y = sM5->TRANSITION_GAP_LONG_OPEN_X;
    sM5->TRANSITION_GAP_LONG_EXTEND_Y = sM5->TRANSITION_GAP_LONG_EXTEND_X;
    sM5->TRANSITION_GAP_LONG_SWITCH_TO_Y = sM5->TRANSITION_GAP_LONG_SWITCH_TO_X;

    emissions_em_loadMatchProbsSymmetrically(sM5->model.EMISSION_MATCH_PROBS, hmm, match);
    int64_t xGapStates[2] = { shortGapX, longGapX };
    int64_t yGapStates[2] = { shortGapY, longGapY };
    emissions_em_loadGapProbs(sM5->model.EMISSION_GAP_X_PROBS, hmm, xGapStates, 2, yGapStates, 2);
    emissions_em_loadGapProbs(sM5->model.EMISSION_GAP_Y_PROBS, hmm, xGapStates, 2, yGapStates, 2);
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//Three state state-machine [StateMachine3 and StateMachine3Vanilla]
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

/////////////////////////////////////////// STATIC FUNCTIONS ////////////////////////////////////////////////////////

// Transitions //
static double stateMachine3_startStateProb(StateMachine *sM, int64_t state) {
    //Match state is like going to a match.
    state_check(sM, state);
    return state == match ? 0 : LOG_ZERO;
}

static double stateMachine3_raggedStartStateProb(StateMachine *sM, int64_t state) {
    state_check(sM, state);
    return (state == shortGapX || state == shortGapY) ? 0 : LOG_ZERO;
}

static double stateMachine3Vanilla_endStateProb(StateMachine *sM, int64_t state) {
    StateMachine3Vanilla *sM3v = (StateMachine3Vanilla *) sM;
    state_check(sM, state);
    switch (state) {
        case match:
            return sM3v->DEFAULT_END_MATCH_PROB;
        case shortGapX:
            return sM3v->DEFAULT_END_FROM_X_PROB;
        case shortGapY:
            return sM3v->DEFAULT_END_FROM_Y_PROB;
    }
    return 0.0;
}

static double stateMachine3_endStateProb(StateMachine *sM, int64_t state) {
    //End state is like to going to a match
    StateMachine3 *sM3 = (StateMachine3 *) sM;
    state_check(sM, state);
    switch (state) {
    case match:
        return sM3->TRANSITION_MATCH_CONTINUE;
    case shortGapX:
        return sM3->TRANSITION_MATCH_FROM_GAP_X;
    case shortGapY:
        return sM3->TRANSITION_MATCH_FROM_GAP_Y;
    }
    return 0.0;
}

static double stateMachine3_raggedEndStateProb(StateMachine *sM, int64_t state) {
    //End state is like to going to a match
    StateMachine3 *sM3 = (StateMachine3 *) sM;
    state_check(sM, state);
    switch (state) {
    case match:
        return (sM3->TRANSITION_GAP_OPEN_X + sM3->TRANSITION_GAP_OPEN_Y) / 2.0;
    case shortGapX:
        return sM3->TRANSITION_GAP_EXTEND_X;
    case shortGapY:
        return sM3->TRANSITION_GAP_EXTEND_Y;
    }
    return 0.0;
}

static void stateMachine3_setTransitionsToDefaults(StateMachine *sM) {
    /*
     * Migrate this into stateMachine constructors
     */
    StateMachine3 *sM3 = (StateMachine3 *) sM;
    sM3->TRANSITION_MATCH_CONTINUE = -0.030064059121770816; //0.9703833696510062f
    sM3->TRANSITION_MATCH_FROM_GAP_X = -1.272871422049609; //1.0 - gapExtend - gapSwitch = 0.280026392297485
    sM3->TRANSITION_MATCH_FROM_GAP_Y = -1.272871422049609; //1.0 - gapExtend - gapSwitch = 0.280026392297485
    sM3->TRANSITION_GAP_OPEN_X = -4.21256642; //0.0129868352330243
    sM3->TRANSITION_GAP_OPEN_Y = -4.21256642; //0.0129868352330243
    sM3->TRANSITION_GAP_EXTEND_X = -0.3388262689231553; //0.7126062401851738f;
    sM3->TRANSITION_GAP_EXTEND_Y = -0.3388262689231553; //0.7126062401851738f;
    sM3->TRANSITION_GAP_SWITCH_TO_X = -4.910694825551255; //0.0073673675173412815f;
    sM3->TRANSITION_GAP_SWITCH_TO_Y = -4.910694825551255; //0.0073673675173412815f;
}

static void stateMachine3_cellCalculate(StateMachine *sM,
                                        double *current, double *lower, double *middle, double *upper,
                                        void *cX, void *cY,
                                        void (*doTransition)(double *, double *,
                                                             int64_t, int64_t,
                                                             double, double,
                                                             void *),
                                        void *extraArgs) {
    StateMachine3 *sM3 = (StateMachine3 *) sM;
    if (lower != NULL) {
        double eP = sM3->getXGapProbFcn(sM3->model.EMISSION_GAP_X_PROBS, cX);
        doTransition(lower, current, match, shortGapX, eP, sM3->TRANSITION_GAP_OPEN_X, extraArgs);
        doTransition(lower, current, shortGapX, shortGapX, eP, sM3->TRANSITION_GAP_EXTEND_X, extraArgs);
        doTransition(lower, current, shortGapY, shortGapX, eP, sM3->TRANSITION_GAP_SWITCH_TO_X, extraArgs);
    }
    if (middle != NULL) {
        double eP = sM3->getMatchProbFcn(sM3->model.EMISSION_MATCH_PROBS, cX, cY);
        doTransition(middle, current, match, match, eP, sM3->TRANSITION_MATCH_CONTINUE, extraArgs);
        doTransition(middle, current, shortGapX, match, eP, sM3->TRANSITION_MATCH_FROM_GAP_X, extraArgs);
        doTransition(middle, current, shortGapY, match, eP, sM3->TRANSITION_MATCH_FROM_GAP_Y, extraArgs);

    }
    if (upper != NULL) {
        double eP = sM3->getYGapProbFcn(sM3->model.EMISSION_GAP_Y_PROBS, cY);
        doTransition(upper, current, match, shortGapY, eP, sM3->TRANSITION_GAP_OPEN_Y, extraArgs);
        doTransition(upper, current, shortGapY, shortGapY, eP, sM3->TRANSITION_GAP_EXTEND_Y, extraArgs);
        doTransition(upper, current, shortGapX, shortGapY, eP, sM3->TRANSITION_GAP_SWITCH_TO_Y, extraArgs);
    }
}

static void stateMachine3Vanilla_cellCalculate(StateMachine *sM,
                                               double *current, double *lower, double *middle, double *upper,
                                               void *cX, void *cY,
                                               void (*doTransition)(double *, double *, int64_t, int64_t,
                                                                    double, double, void *),
                                               void *extraArgs) {

    StateMachine3Vanilla *sM3v = (StateMachine3Vanilla *) sM;
    // Establish transition probs (all adopted from Nanopolish by JTS)
    // from match
    double a_mx = sM3v->getKmerSkipProb((StateMachine *) sM3v, cX);
    double a_me = (1 - a_mx) * sM3v->TRANSITION_M_TO_Y_NOT_X; // trans M to Y not X fudge factor
    double a_mm = 1.0f - a_me - a_mx;
    // from Y [Extra event state]
    double a_ee = sM3v->TRANSITION_E_TO_E; //
    double a_em = 1.0f - a_ee;
    // from X [Skipped event state]
    double a_xx = a_mx;
    double a_xm = 1.0f - a_xx;

    if (lower != NULL) {
        doTransition(lower, current, match, shortGapX, 0, log(a_mx), extraArgs);
        doTransition(lower, current, shortGapX, shortGapX, 0, log(a_xx), extraArgs);
        // X to Y not allowed
        //doTransition(lower, current, shortGapY, shortGapX, eP, sM3->TRANSITION_GAP_SWITCH_TO_X, extraArgs);
    }
    if (middle != NULL) {
        double eP = sM3v->getMatchProbFcn(sM3v->model.EMISSION_MATCH_PROBS, cX, cY);
        doTransition(middle, current, match, match, eP, log(a_mm), extraArgs);
        doTransition(middle, current, shortGapX, match, eP, log(a_xm), extraArgs);
        doTransition(middle, current, shortGapY, match, eP, log(a_em), extraArgs);
    }
    if (upper != NULL) {
        double eP = sM3v->getScaledMatchProbFcn(sM3v->model.EMISSION_GAP_Y_PROBS, cX, cY);
        doTransition(upper, current, match, shortGapY, eP, log(a_me), extraArgs);
        doTransition(upper, current, shortGapY, shortGapY, eP, log(a_ee), extraArgs);
        // Y to X not allowed
        //doTransition(upper, current, shortGapX, shortGapY, eP, sM3->TRANSITION_GAP_SWITCH_TO_Y, extraArgs);
    }
}

///////////////////////////////////////////// CORE FUNCTIONS ////////////////////////////////////////////////////////

StateMachine *stateMachine3_construct(StateMachineType type, int64_t parameterSetSize,
                                      void (*setEmissionsDefaults)(StateMachine *sM),
                                      double (*gapXProbFcn)(const double *, void *),
                                      double (*gapYProbFcn)(const double *, void *),
                                      double (*matchProbFcn)(const double *, void *, void *)) {
    /*
     * Description of (potentially ambigious) arguments:
     * parameterSetSize = the number of kmers that we are using, of len(kmer) = 1, then the number is 4 (or 5 if we're
     * including N). It's 25 if len(kmer) = 2, it's 4096 in the 6-mer model.
     *
     */
    StateMachine3 *sM3 = st_malloc(sizeof(StateMachine3));
    if (type != threeState && type != threeStateAsymmetric) {
        st_errAbort("Tried to create a three state state-machine with the wrong type");
    }
    // setup transitions
    sM3->TRANSITION_MATCH_CONTINUE = -0.030064059121770816; //0.9703833696510062f
    sM3->TRANSITION_MATCH_FROM_GAP_X = -1.272871422049609; //1.0 - gapExtend - gapSwitch = 0.280026392297485
    sM3->TRANSITION_MATCH_FROM_GAP_Y = -1.272871422049609; //1.0 - gapExtend - gapSwitch = 0.280026392297485
    sM3->TRANSITION_GAP_OPEN_X = -4.21256642; //0.0129868352330243
    sM3->TRANSITION_GAP_OPEN_Y = -4.21256642; //0.0129868352330243
    sM3->TRANSITION_GAP_EXTEND_X = -0.3388262689231553; //0.7126062401851738f;
    sM3->TRANSITION_GAP_EXTEND_Y = -0.3388262689231553; //0.7126062401851738f;
    sM3->TRANSITION_GAP_SWITCH_TO_X = -4.910694825551255; //0.0073673675173412815f;
    sM3->TRANSITION_GAP_SWITCH_TO_Y = -4.910694825551255; //0.0073673675173412815f;
    // setup the parent class
    sM3->model.type = type;
    sM3->model.parameterSetSize = parameterSetSize;
    sM3->model.stateNumber = 3;
    sM3->model.matchState = match;
    sM3->model.startStateProb = stateMachine3_startStateProb;
    sM3->model.endStateProb = stateMachine3_endStateProb;
    sM3->model.raggedStartStateProb = stateMachine3_raggedStartStateProb;
    sM3->model.raggedEndStateProb = stateMachine3_raggedEndStateProb;
    sM3->getXGapProbFcn = gapXProbFcn;
    sM3->getYGapProbFcn = gapYProbFcn;
    sM3->getMatchProbFcn = matchProbFcn;
    sM3->model.cellCalculate = stateMachine3_cellCalculate;
    // set emissions to defaults or zeros
    setEmissionsDefaults((StateMachine *) sM3);

    return (StateMachine *) sM3;
}

StateMachine *stateMachine3Vanilla_construct(StateMachineType type, int64_t parameterSetSize,
                                             void (*setEmissionsDefaults)(StateMachine *sM),
                                             double (*xSkipProbFcn)(StateMachine *, void *),
                                             double (*scaledMatchProbFcn)(const double *, void *, void *),
                                             double (*matchProbFcn)(const double *, void *, void *)) {
    /*
     * State machine for basic event signal to sequence alignment
     */
    // upcast
    StateMachine3Vanilla *sM3v = st_malloc(sizeof(StateMachine3Vanilla));
    // check
    if (type != threeState && type != threeStateAsymmetric) {
        st_errAbort("Tried to create a vanilla state machine with the wrong type?");
    }
    // setup end state prob defaults
    // defaults from a template nanopore file
    sM3v->TRANSITION_M_TO_Y_NOT_X = 0.17; //default for template
    sM3v->TRANSITION_E_TO_E = 0.55f; //default for template
    sM3v->DEFAULT_END_MATCH_PROB = 0.79015888282447311; // stride_prb
    sM3v->DEFAULT_END_FROM_X_PROB = 0.19652425498269727; // skip_prob
    sM3v->DEFAULT_END_FROM_Y_PROB = 0.013316862192910478; // stay_prob
    // setup the parent class
    sM3v->model.type = type;
    sM3v->model.parameterSetSize = parameterSetSize;
    sM3v->model.stateNumber = 3;
    sM3v->model.matchState = match;
    sM3v->model.startStateProb = stateMachine3_startStateProb;
    sM3v->model.raggedStartStateProb = stateMachine3_raggedStartStateProb;
    sM3v->model.endStateProb = stateMachine3Vanilla_endStateProb;
    sM3v->model.raggedEndStateProb = stateMachine3Vanilla_endStateProb;
    sM3v->model.cellCalculate = stateMachine3Vanilla_cellCalculate;

    // stateMachine3Vanilla-specific functions
    sM3v->getKmerSkipProb = xSkipProbFcn;
    sM3v->getScaledMatchProbFcn = scaledMatchProbFcn;
    sM3v->getMatchProbFcn = matchProbFcn;

    // set emissions to defaults or zeros
    setEmissionsDefaults((StateMachine *) sM3v);
    return (StateMachine *) sM3v;
}

/*
static void stateMachine3_loadAsymmetric(StateMachine3 *sM3, Hmm *hmm) {
    if (hmm->type != threeStateAsymmetric) {
        st_errAbort("Wrong hmm type");
    }
    sM3->TRANSITION_MATCH_CONTINUE = log(hmm_getTransition(hmm, match, match));
    sM3->TRANSITION_MATCH_FROM_GAP_X = log(hmm_getTransition(hmm, shortGapX, match));
    sM3->TRANSITION_MATCH_FROM_GAP_Y = log(hmm_getTransition(hmm, shortGapY, match));
    sM3->TRANSITION_GAP_OPEN_X = log(hmm_getTransition(hmm, match, shortGapX));
    sM3->TRANSITION_GAP_OPEN_Y = log(hmm_getTransition(hmm, match, shortGapY));
    sM3->TRANSITION_GAP_EXTEND_X = log(hmm_getTransition(hmm, shortGapX, shortGapX));
    sM3->TRANSITION_GAP_EXTEND_Y = log(hmm_getTransition(hmm, shortGapY, shortGapY));
    sM3->TRANSITION_GAP_SWITCH_TO_X = log(hmm_getTransition(hmm, shortGapY, shortGapX));
    sM3->TRANSITION_GAP_SWITCH_TO_Y = log(hmm_getTransition(hmm, shortGapX, shortGapY));
    emissions_em_loadMatchProbs(sM3->EMISSION_MATCH_PROBS, hmm, match);
    int64_t xGapStates[1] = { shortGapX };
    int64_t yGapStates[1] = { shortGapY };
    emissions_loadGapProbs(sM3->EMISSION_GAP_X_PROBS, hmm, xGapStates, 1, NULL, 0);
    emissions_loadGapProbs(sM3->EMISSION_GAP_Y_PROBS, hmm, NULL, 0, yGapStates, 1);
}

static void stateMachine3_loadSymmetric(StateMachine3 *sM3, Hmm *hmm) {
    if (hmm->type != threeState) {
        st_errAbort("Wrong hmm type");
    }
    sM3->TRANSITION_MATCH_CONTINUE = log(hmm_getTransition(hmm, match, match));
    sM3->TRANSITION_MATCH_FROM_GAP_X = log(
            (hmm_getTransition(hmm, shortGapX, match) + hmm_getTransition(hmm, shortGapY, match)) / 2.0);
    sM3->TRANSITION_MATCH_FROM_GAP_Y = sM3->TRANSITION_MATCH_FROM_GAP_X;
    sM3->TRANSITION_GAP_OPEN_X = log(
            (hmm_getTransition(hmm, match, shortGapX) + hmm_getTransition(hmm, match, shortGapY)) / 2.0);
    sM3->TRANSITION_GAP_OPEN_Y = sM3->TRANSITION_GAP_OPEN_X;
    sM3->TRANSITION_GAP_EXTEND_X = log(
            (hmm_getTransition(hmm, shortGapX, shortGapX) + hmm_getTransition(hmm, shortGapY, shortGapY)) / 2.0);
    sM3->TRANSITION_GAP_EXTEND_Y = sM3->TRANSITION_GAP_EXTEND_X;
    sM3->TRANSITION_GAP_SWITCH_TO_X = log(
            (hmm_getTransition(hmm, shortGapY, shortGapX) + hmm_getTransition(hmm, shortGapX, shortGapY)) / 2.0);
    sM3->TRANSITION_GAP_SWITCH_TO_Y = sM3->TRANSITION_GAP_SWITCH_TO_X;
    emissions_em_loadMatchProbsSymmetrically(sM3->EMISSION_MATCH_PROBS, hmm, match);
    int64_t xGapStates[2] = { shortGapX };
    int64_t yGapStates[2] = { shortGapY };
    emissions_loadGapProbs(sM3->EMISSION_GAP_X_PROBS, hmm, xGapStates, 1, yGapStates, 1);
    emissions_em_loadGapProbs(sM3->EMISSION_GAP_Y_PROBS, hmm, xGapStates, 1, yGapStates, 1);
}
*/

///////////////////////////////////
///////////////////////////////////
//Public functions
///////////////////////////////////
///////////////////////////////////

StateMachine *getStateMachine5(Hmm *hmmD, StateMachineFunctions *sMfs) {
    if (hmmD->type == fiveState) {
        StateMachine5 *sM5 = (StateMachine5 *) stateMachine5_construct(fiveState, hmmD->symbolSetSize,
                                                                       emissions_discrete_initEmissionsToZero,
                                                                       sMfs->gapXProbFcn,
                                                                       sMfs->gapYProbFcn,
                                                                       sMfs->matchProbFcn);
        stateMachine5_loadSymmetric(sM5, hmmD);
        return (StateMachine *) sM5;
    }
    if (hmmD->type == fiveStateAsymmetric) {
        StateMachine5 *sM5 = (StateMachine5 *) stateMachine5_construct(fiveState, hmmD->symbolSetSize,
                                                                       emissions_discrete_initEmissionsToZero,
                                                                       sMfs->gapXProbFcn,
                                                                       sMfs->gapYProbFcn,
                                                                       sMfs->matchProbFcn);
        stateMachine5_loadAsymmetric(sM5, hmmD);
        return (StateMachine *) sM5;
    }
}

StateMachine *getSignalStateMachine3(const char *modelFile) {
    // construct a stateMachine3Vanilla then load the model
    StateMachine *sM3v = stateMachine3Vanilla_construct(threeState, NUM_OF_KMERS,
                                                        emissions_signal_initEmissionsToZero,
                                                        emissions_signal_getKmerSkipProb,
                                                        emissions_signal_getlogGaussPDFMatchProb,
                                                        emissions_signal_getlogGaussPDFMatchProb);

    emissions_signal_loadPoreModel(sM3v, modelFile);
    return sM3v;
}

void stateMachine_destruct(StateMachine *stateMachine) {
    free(stateMachine);
}

