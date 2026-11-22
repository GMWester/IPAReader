/* Internal acoustic constants. They can be overridden for research/tuning with the
 * environment variable IPR_TUNE="name=value,name=value" (read once per process). */
#ifndef IPR_TUNE_H
#define IPR_TUNE_H
#define IPR_TUNE_LIST(X) \
    X(V_BASE, 72) X(V_OPEN, 32) X(V_GLIDE, 0.55) \
    X(D_STRESS1, 1.4) X(D_STRESS2, 1.15) X(D_UNSTR, 0.85) X(D_FINAL, 1.3) X(D_CLUSTER, 0.82) \
    X(D_CONS, 0.75) X(D_CONS_FINAL, 1.15) X(D_STOP_VL, 60) X(D_STOP_VD, 50) X(D_FRIC, 1.0) \
    X(D_NASAL, 62) X(D_LAT, 58) X(D_APPROX, 48) X(D_RHO, 58) X(D_PAUSE, 1.0) \
    X(BURST_LEN, 1.33) X(BURST_DB, 0) X(VOT_UNASP, 14) X(VOT_ASP, 60) X(VOICEBAR, 0.12) \
    X(VD_BURST, 0.6) X(ASP_AH, 0.5) X(UNASP_AH, 0.3) \
    X(FRIC_DB, 0) X(SIB_DB, 0) X(NONSIB_DB, 0) X(VFRIC_AV, 0.5) X(VFRIC_NOISE, 0.55) \
    X(NASAL_AV, 0.8) X(NASAL_BZ, 150) X(NASALIZE, 120) X(LAT_AV, 1.0) X(APPROX_AV, 0.8) \
    X(TR_STOP, 40) X(TR_NASAL, 40) X(TR_LAT, 22) X(TR_APPROX, 60) X(TR_FRIC, 35) X(TR_TAP, 30) \
    X(K_SCALE, 1.0) X(F1_EXPAND, 1.0) X(F2_EXPAND, 1.0) \
    X(B1K, 1.0) X(B2K, 1.0) X(B3K, 1.0) X(B45K, 1.0) \
    X(ACC1, 3.2) X(DECL, 1.2) X(FINAL_FALL, 3.5) \
    X(RAD, 0.8) X(SM_F, 9) X(SM_AV, 7) X(H_AH, 0.8) X(VOWEL_AV_H, 0.5) \
    X(MICRO_VL, 0.08) X(MICRO_VD, 0.05) X(STRESS_AMP, 0.15) \
    X(K_NASAL, 0.15) X(M_F2, 900) X(N_F2, 1800) X(VAFF_NOISE, 0.9) \
    X(N_ZERO, 1.3) X(M_ZERO, 1.0) X(NG_ZERO, 1.2) X(SH_SHIFT, 1.0) \
    X(NASV_Z, 150) X(NASV_B1, 0.6) X(NASV_F1, 0) X(UVU_AV, 0.6) X(UVU_DB, 0) X(SEC_GLIDE, 0.7) X(SEC_GLIDE_MS, 45) \
    X(RET_MS, 0.5) X(HFN, 0.04) X(FINAL_CREAK, 0.25) X(FINAL_DECAY, 0.2) \
    X(EQ_LO_F, 400) X(EQ_LO_DB, -3) X(EQ_MID_F, 2000) X(EQ_MID_DB, 5) X(EQ_GAIN, 0.9) X(NOISE_GAIN, 0.85) X(FLUTTER, 0.35) \
    X(D_H, 75) X(K_NG, 0.4) X(VSIB_BOOST, 1.4) \
    X(LF_ON, 1) X(LF_GAIN, 3.2) X(RD_BASE, 1.0) X(RD_SLOPE, 3.0) X(CURVE, 1) \
    X(OQ_STRESS, 0.05) X(OQ_FINAL, 0.12) X(SYL_ENV, 0.2) X(SM_F0, 45) X(VOFF_ASP, 0.12) \
    X(PEAK_POS, 0.68) X(ACC_START, 0.15) X(VOICING_DUR, 0.18) \
    X(ACC_FIRST, 0.15) X(ACC_DOWN, 0.08) X(VDRIFT, 0.12) X(GLIDE_RAISE, 0.6) \
    X(PINCH, 300) X(NG_BW, 1.0) X(N_F3, 2800) X(M_BW, 1.3) X(N_BW, 1.0) X(PINCH_MODE, 2) X(PINCH_BACK, 1000) X(K_NG_BACK, 0.9) X(NREL, 90) X(MUR_F1, 300) X(MUR_B1, 150) X(MUR_BNP, 100) \
    X(MUR_PAR, 0) X(MUR_M, 1050) X(MUR_N, 1700) X(MUR_NG, 2300) X(MUR_NG_A, 1.0) X(NREL_NG, 90) X(CIT_NG_BW, 1.0) X(MUR_TRACT, 1) X(CIT_FNP, 1000) X(CIT_BNP, 300) X(MUR_F2, 1050) X(MUR_F3, 2150)

enum {
#define X(n, d) TU_##n,
    IPR_TUNE_LIST(X)
#undef X
    TU_COUNT
};
extern float ipr_tune[TU_COUNT];
void ipr_tune_init(void);
#define T(n) (ipr_tune[TU_##n])
#endif
