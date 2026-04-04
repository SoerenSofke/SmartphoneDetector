/*
 * ESP32 USB MIDI Host Library (ESP32 USB MIDI Omocha)
 * Copyright (c) 2025 ndenki
 * https://github.com/enudenki/esp32-usb-host-midi-library.git
 *
 * Modified for ESP32-P4 compatibility, robustness, and performance.
 */
#ifndef USBMIDI_H
#define USBMIDI_H

#include <Arduino.h>
#include <usb/usb_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>

/* ── Target detection ───────────────────────────────────────────── */
#if CONFIG_IDF_TARGET_ESP32P4
  #define USB_MIDI_TARGET_P4  1
#else
  #define USB_MIDI_TARGET_P4  0
#endif

/* ── Tunables ───────────────────────────────────────────────────── */
#define MIDI_OUT_QUEUE_SIZE          128
#define NUM_MIDI_IN_TRANSFERS        2
#define MAX_CLIENT_EVENT_MESSAGES    5
#define USB_EVENT_POLL_TICKS         1
#define USB_AUDIO_SUBCLASS_MIDI_STREAMING 3

/* Maximum consecutive IN-transfer errors before giving up re-submit */
#define MIDI_IN_MAX_ERROR_COUNT      10

/* OUT-transfer timeout in milliseconds */
#define MIDI_OUT_TIMEOUT_MS          1000

/*
 * Minimum descriptor length: A valid USB descriptor must have at
 * least 2 bytes (bLength + bDescriptorType).  Interface descriptors
 * are 9 bytes, endpoint descriptors are 7 bytes.
 */
#define USB_DESC_MIN_LENGTH          2
#define USB_INTF_DESC_MIN_LENGTH     9
#define USB_EP_DESC_MIN_LENGTH       7

/* ── MIDI CIN codes ─────────────────────────────────────────────── */
enum class MidiCin : uint8_t {
    NOTE_OFF        = 0x08,
    NOTE_ON         = 0x09,
    CONTROL_CHANGE  = 0x0B,
    PROGRAM_CHANGE  = 0x0C,
};

/* ── Class ──────────────────────────────────────────────────────── */
class UsbMidi {
public:
    using MidiMessageCallback = void (*)(const uint8_t (&)[4]);

    UsbMidi();
    ~UsbMidi();

    bool begin();
    void update();

    void onMidiMessage(MidiMessageCallback callback);

    bool sendMidiMessage(const uint8_t* message, uint8_t size = 4);
    bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    bool noteOff(uint8_t channel, uint8_t note, uint8_t velocity);
    bool controlChange(uint8_t channel, uint8_t controller, uint8_t value);
    bool programChange(uint8_t channel, uint8_t program);

    size_t getQueueAvailableSize() const;

    void onDeviceConnected(void (*callback)());
    void onDeviceDisconnected(void (*callback)());

private:
    static void _clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg);
    static void _midiTransferCallback(usb_transfer_t* transfer);

    void _handleClientEvent(const usb_host_client_event_msg_t* eventMsg);
    void _handleMidiTransfer(usb_transfer_t* transfer);
    void _parseConfigDescriptor(const usb_config_desc_t* configDesc);
    void _findAndClaimMidiInterface(const usb_intf_desc_t* intf);
    void _setupMidiEndpoints(const usb_ep_desc_t* endpoint);
    void _setupMidiInEndpoint(const usb_ep_desc_t* endpoint);
    void _setupMidiOutEndpoint(const usb_ep_desc_t* endpoint);
    void _cancelInFlightTransfers();
    void _releaseDeviceResources(bool deviceGone = false);
    void _processMidiOutQueue();
    void _checkOutTransferTimeout();
    void _resubmitPendingInTransfers();

    usb_host_client_handle_t _clientHandle;
    usb_device_handle_t      _deviceHandle;
    usb_transfer_t*          _midiOutTransfer;
    usb_transfer_t*          _midiInTransfers[NUM_MIDI_IN_TRANSFERS];
    QueueHandle_t            _midiOutQueue;

    uint8_t _midiInterfaceNumber;
    uint8_t _midiInEpAddr;
    uint8_t _midiOutEpAddr;

    bool                _isMidiInterfaceFound;
    std::atomic<bool>   _areEndpointsReady;
    std::atomic<bool>   _isMidiOutBusy;

    /* IN-transfer error tracking (per transfer slot) */
    uint8_t _midiInErrorCount[NUM_MIDI_IN_TRANSFERS];

    /* Deferred IN-transfer re-submission flags (per transfer slot) */
    bool _pendingInResubmit[NUM_MIDI_IN_TRANSFERS];

    /* Deferred disconnect – set in callback, acted on in update() */
    bool _deviceGonePending;

    /* OUT-transfer timeout tracking */
    uint32_t _midiOutSubmitTime;

    /* Callbacks – set before begin() or from the same task as update() */
    MidiMessageCallback  _midiMessageCallback;
    void (*_deviceConnectedCallback)();
    void (*_deviceDisconnectedCallback)();

    bool _isHostInstalled;
};

#endif // USBMIDI_H
