#
# Test probing MPEGTS format and codecs
#
PROBE_CODEC_NAME_COMMAND = \
    ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=codec_name \
    -print_format default -bitexact -v 0

FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-latm
fate-mpegts-probe-latm: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-latm: CMD = run $(PROBE_CODEC_NAME_COMMAND) -i "$(SRC)"


FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-program
fate-mpegts-probe-program: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-program: CMD = run $(PROBE_CODEC_NAME_COMMAND) -select_streams p:769:v:0 -i "$(SRC)"


FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS) += fate-mpegts-probe-pmt-merge
fate-mpegts-probe-pmt-merge: SRC = $(TARGET_SAMPLES)/mpegts/pmtchange.ts
fate-mpegts-probe-pmt-merge: CMD = run $(PROBE_CODEC_NAME_COMMAND) -merge_pmt_versions 1 -i "$(SRC)"


FATE_SAMPLES_FFPROBE += $(FATE_MPEGTS_PROBE-yes)

#
# Test AV1 muxing/demuxing in MPEG-TS
#
# AV1 muxing: IVF -> MPEG-TS (Section 5 to start code format)
FATE_MPEGTS_AV1-$(call REMUX, MPEGTS, IVF_DEMUXER AV1_PARSER) += fate-mpegts-av1-mux
fate-mpegts-av1-mux: SRC = $(TARGET_SAMPLES)/av1-test-vectors/av1-1-b8-05-mv.ivf
fate-mpegts-av1-mux: CMD = framecrc -i "$(SRC)" -c:v copy -f mpegts

# AV1 roundtrip: IVF -> MPEG-TS -> IVF (tests both muxing and demuxing)
FATE_MPEGTS_AV1-$(call REMUX, IVF MPEGTS, IVF_DEMUXER MPEGTS_DEMUXER AV1_PARSER AV1_TSTOSECTION5_BSF) += fate-mpegts-av1-roundtrip
fate-mpegts-av1-roundtrip: SRC = $(TARGET_SAMPLES)/av1-test-vectors/av1-1-b8-05-mv.ivf
fate-mpegts-av1-roundtrip: CMD = transcode ivf "$(SRC)" mpegts "-c:v copy" "-c:v copy -f ivf"

FATE_SAMPLES_FFMPEG += $(FATE_MPEGTS_AV1-yes)

fate-mpegts: $(FATE_MPEGTS_PROBE-yes) $(FATE_MPEGTS_AV1-yes)
