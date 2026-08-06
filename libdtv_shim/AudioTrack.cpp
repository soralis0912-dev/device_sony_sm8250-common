/*
 * Copyright (C) 2026 The WitAqua Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <media/AudioTrack.h>
#include <utils/RefBase.h>

#include <cstddef>
#include <cstdint>

/*
 * libDtvULayer.so was built against Android 12, where
 *
 *     class AudioDeviceCallback : public RefBase
 *
 * so android::AudioTrack carried its RefBase at offset 0. AOSP 161f47e0a4
 * ("Inherit RefBase virtually in AudioDeviceCallback", Android 13) made that
 * inheritance virtual, which moves the RefBase subobject out of the front of
 * the object.
 *
 * The constructor signature also changed, but libaudioclient_shim already
 * covers that. What it does not cover is the two things the blob inlined
 * against the old layout:
 *
 *     sp<AudioTrack> track = new AudioTrack(...);   -> RefBase::incStrong(this + 0)
 *     if (track->initCheck() != NO_ERROR)           -> ldr w8, [this + 0x210]
 *
 * With RefBase no longer at the front, incStrong() reads a null mRefs and
 * dtvmgr dies with SIGSEGV before it ever opens the tuner.
 *
 * So hand the blob back the layout it was compiled against: RefBase at offset
 * 0, mStatus at 0x210, and a real AudioTrack held inside. extract-files.py
 * renames the AudioTrack symbols the blob imports to AudioTrkCt so they land
 * here instead of on libaudioclient. That name is 10 characters, like
 * AudioTrack, so every mangled name keeps its length and .dynstr can be
 * patched in place.
 */

namespace android {

namespace {

using legacy_callback_t = void (*)(int event, void* user, void* info);

// Android 12 delivered events through a function pointer; the current API takes
// an interface.
class LegacyCallback : public AudioTrack::IAudioTrackCallback {
  public:
    LegacyCallback(legacy_callback_t callback, void* user) : mCallback(callback), mData(user) {}

    size_t onMoreData(const AudioTrack::Buffer& buffer) override {
        AudioTrack::Buffer copy = buffer;
        mCallback(AudioTrack::EVENT_MORE_DATA, mData, &copy);
        return copy.size();
    }
    void onUnderrun() override { mCallback(AudioTrack::EVENT_UNDERRUN, mData, nullptr); }
    void onLoopEnd(int32_t loopsRemaining) override {
        mCallback(AudioTrack::EVENT_LOOP_END, mData, &loopsRemaining);
    }
    void onMarker(uint32_t markerPosition) override {
        mCallback(AudioTrack::EVENT_MARKER, mData, &markerPosition);
    }
    void onNewPos(uint32_t newPos) override {
        mCallback(AudioTrack::EVENT_NEW_POS, mData, &newPos);
    }
    void onBufferEnd() override { mCallback(AudioTrack::EVENT_BUFFER_END, mData, nullptr); }
    void onNewIAudioTrack() override {
        mCallback(AudioTrack::EVENT_NEW_IAUDIOTRACK, mData, nullptr);
    }
    void onStreamEnd() override { mCallback(AudioTrack::EVENT_STREAM_END, mData, nullptr); }
    size_t onCanWriteMoreData(const AudioTrack::Buffer& buffer) override {
        AudioTrack::Buffer copy = buffer;
        mCallback(AudioTrack::EVENT_CAN_WRITE_MORE_DATA, mData, &copy);
        return copy.size();
    }

  private:
    const legacy_callback_t mCallback;
    void* const mData;
};

constexpr size_t kStatusOffset = 0x210;  // offsetof(android 12 AudioTrack, mStatus)
constexpr size_t kObjectSize = 0x4e8;    // sizeof(android 12 AudioTrack)

}  // namespace

class AudioTrkCt : public RefBase {
  public:
    /*
     * Only here so that the constructor mangles with NS0_13transfer_typeE, the
     * way it did when transfer_type was nested in AudioTrack. The values match
     * android::AudioTrack::transfer_type, which has not changed since.
     */
    enum transfer_type {
        TRANSFER_DEFAULT,
        TRANSFER_CALLBACK,
        TRANSFER_OBTAIN,
        TRANSFER_SYNC,
        TRANSFER_SHARED,
        TRANSFER_SYNC_NOTIF_CALLBACK,
    };

    AudioTrkCt(audio_stream_type_t streamType, uint32_t sampleRate, audio_format_t format,
               audio_channel_mask_t channelMask, size_t frameCount, audio_output_flags_t flags,
               legacy_callback_t cbf, void* user, int32_t notificationFrames,
               audio_session_t sessionId, transfer_type transferType,
               const audio_offload_info_t* offloadInfo,
               const content::AttributionSourceState& attributionSource,
               const audio_attributes_t* pAttributes, bool doNotReconnect, float maxRequiredSpeed,
               audio_port_handle_t selectedDeviceId);
    ~AudioTrkCt() override;

    // Defined out of line: the blob imports these, so they have to be real
    // exported symbols rather than inlined away.
    status_t start();
    void stop();
    void flush();
    bool stopped() const;
    status_t setSampleRate(uint32_t sampleRate);
    status_t setVolume(float left, float right);
    status_t setVolume(float volume);
    ssize_t write(const void* buffer, size_t size, bool blocking);

  private:
    // Never called; it just gives the layout checks somewhere with access to
    // the members and a complete AudioTrkCt.
    static void checkLayout();

    uint8_t mReserved0[kStatusOffset - sizeof(RefBase)];
    status_t mStatus;
    sp<AudioTrack> mTrack;
    // The current AudioTrack only keeps a weak reference to the callback, so
    // somebody has to own it.
    sp<AudioTrack::IAudioTrackCallback> mCallback;
    // Padded out so that the sized operator delete() the destructor emits
    // matches the operator new(0x4e8) the blob paired it with.
    uint8_t mReserved1[kObjectSize - kStatusOffset - 24];
};

void AudioTrkCt::checkLayout() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(AudioTrkCt, mStatus) == kStatusOffset, "mStatus must stay at 0x210");
    static_assert(sizeof(AudioTrkCt) == kObjectSize, "the blob allocates exactly this many bytes");
#pragma clang diagnostic pop
}

AudioTrkCt::AudioTrkCt(audio_stream_type_t streamType, uint32_t sampleRate, audio_format_t format,
                       audio_channel_mask_t channelMask, size_t frameCount,
                       audio_output_flags_t flags, legacy_callback_t cbf, void* user,
                       int32_t notificationFrames, audio_session_t sessionId,
                       transfer_type transferType, const audio_offload_info_t* offloadInfo,
                       const content::AttributionSourceState& attributionSource,
                       const audio_attributes_t* pAttributes, bool doNotReconnect,
                       float maxRequiredSpeed, audio_port_handle_t selectedDeviceId)
    : mReserved0(), mStatus(NO_INIT), mReserved1() {
    if (cbf != nullptr) {
        mCallback = sp<LegacyCallback>::make(cbf, user);
    }

    mTrack = sp<AudioTrack>::make(streamType, sampleRate, format, channelMask, frameCount, flags,
                                  mCallback, notificationFrames, sessionId,
                                  static_cast<AudioTrack::transfer_type>(transferType), offloadInfo,
                                  attributionSource, pAttributes, doNotReconnect, maxRequiredSpeed,
                                  selectedDeviceId);
    mStatus = mTrack->initCheck();
}

AudioTrkCt::~AudioTrkCt() = default;

status_t AudioTrkCt::start() {
    return mTrack->start();
}

void AudioTrkCt::stop() {
    mTrack->stop();
}

void AudioTrkCt::flush() {
    mTrack->flush();
}

bool AudioTrkCt::stopped() const {
    return mTrack->stopped();
}

status_t AudioTrkCt::setSampleRate(uint32_t sampleRate) {
    return mTrack->setSampleRate(sampleRate);
}

status_t AudioTrkCt::setVolume(float left, float right) {
    return mTrack->setVolume(left, right);
}

status_t AudioTrkCt::setVolume(float volume) {
    return mTrack->setVolume(volume);
}

ssize_t AudioTrkCt::write(const void* buffer, size_t size, bool blocking) {
    return mTrack->write(buffer, size, blocking);
}

}  // namespace android
