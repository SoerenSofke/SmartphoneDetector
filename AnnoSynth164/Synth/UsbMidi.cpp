/*
 * ESP32 USB MIDI Host Library (ESP32 USB MIDI Omocha)
 * Copyright (c) 2025 ndenki
 * https://github.com/enudenki/esp32-usb-host-midi-library.git
 *
 * Modified for ESP32-P4 compatibility, robustness, and performance.
 */
#include "UsbMidi.h"
#include <string.h>

UsbMidi::UsbMidi()
    : _clientHandle(nullptr),
      _deviceHandle(nullptr),
      _midiOutTransfer(nullptr),
      _midiOutQueue(nullptr),
      _midiInterfaceNumber(0),
      _midiInEpAddr(0),
      _midiOutEpAddr(0),
      _isMidiInterfaceFound(false),
      _areEndpointsReady(false),
      _isMidiOutBusy(false),
      _midiOutSubmitTime(0),
      _midiMessageCallback(nullptr),
      _deviceConnectedCallback(nullptr),
      _deviceDisconnectedCallback(nullptr),
      _isHostInstalled(false)
{
    for (int i = 0; i < NUM_MIDI_IN_TRANSFERS; ++i) {
        _midiInTransfers[i] = nullptr;
        _midiInErrorCount[i] = 0;
        _pendingInResubmit[i] = false;
    }
}

UsbMidi::~UsbMidi()
{
    _releaseDeviceResources();

    if (_clientHandle) {
        usb_host_client_deregister(_clientHandle);
        _clientHandle = nullptr;
    }

    if (_isHostInstalled) {
        /* Drain pending library events before uninstall */
        uint32_t eventFlags = 0;
        for (int i = 0; i < 10; ++i) {
            usb_host_lib_handle_events(USB_EVENT_POLL_TICKS, &eventFlags);
            if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) break;
        }
        usb_host_uninstall();
        _isHostInstalled = false;
    }

    if (_midiOutQueue) {
        vQueueDelete(_midiOutQueue);
        _midiOutQueue = nullptr;
    }
}

bool UsbMidi::begin()
{
    _midiOutQueue = xQueueCreate(MIDI_OUT_QUEUE_SIZE, sizeof(uint8_t[4]));
    if (!_midiOutQueue) {
        return false;
    }

    const usb_host_config_t hostConfig = {
#if USB_MIDI_TARGET_P4
        .skip_phy_setup = false,
        .root_port_unpowered = false,
#endif
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    if (usb_host_install(&hostConfig) != ESP_OK) {
        vQueueDelete(_midiOutQueue);
        _midiOutQueue = nullptr;
        return false;
    }
    _isHostInstalled = true;

    const usb_host_client_config_t clientConfig = {
        .is_synchronous = false,
        .max_num_event_msg = MAX_CLIENT_EVENT_MESSAGES,
        .async = { .client_event_callback = _clientEventCallback, .callback_arg = this }
    };
    if (usb_host_client_register(&clientConfig, &_clientHandle) != ESP_OK) {
        usb_host_uninstall();
        _isHostInstalled = false;
        vQueueDelete(_midiOutQueue);
        _midiOutQueue = nullptr;
        return false;
    }

    return true;
}

void UsbMidi::update()
{
    if (!_clientHandle) return;

    /*
     * Process library-level events FIRST.  This is where port-level
     * disconnect detection generates the DEV_GONE event and enqueues
     * it to the client event queue.  By doing this before client
     * events, a transfer completion and the resulting DEV_GONE are
     * both available to usb_host_client_handle_events in the same
     * pass — the disconnect callback runs before we attempt any
     * re-submission.
     */
    usb_host_lib_handle_events(USB_EVENT_POLL_TICKS, nullptr);
    usb_host_client_handle_events(_clientHandle, USB_EVENT_POLL_TICKS);

    /*
     * Re-submit IN transfers that were deferred from the callback.
     * Because DEV_GONE was processed above (same cycle), the
     * _areEndpointsReady flag is already false when the device is
     * gone — the re-submit is skipped cleanly.
     */
    _resubmitPendingInTransfers();

    _checkOutTransferTimeout();
    _processMidiOutQueue();
}

void UsbMidi::onMidiMessage(MidiMessageCallback callback)
{
    _midiMessageCallback = callback;
}

bool UsbMidi::sendMidiMessage(const uint8_t* message, uint8_t size)
{
    if (!_midiOutQueue || !message) return false;
    if (size == 0 || size % 4 != 0) return false;

    size_t numMessages = size / 4;

    if (getQueueAvailableSize() < numMessages) {
        return false;
    }

    for (size_t i = 0; i < numMessages; ++i) {
        const uint8_t* currentMessage = message + (i * 4);
        if (xQueueSend(_midiOutQueue, currentMessage, 0) != pdPASS) {
            return false;
        }
    }

    return true;
}

bool UsbMidi::noteOn(uint8_t channel, uint8_t note, uint8_t velocity)
{
    uint8_t message[4] = {
        (uint8_t)MidiCin::NOTE_ON,
        (uint8_t)(0x90 | (channel & 0x0F)),
        (uint8_t)(note & 0x7F),
        (uint8_t)(velocity & 0x7F)
    };
    return sendMidiMessage(message, 4);
}

bool UsbMidi::noteOff(uint8_t channel, uint8_t note, uint8_t velocity)
{
    uint8_t message[4] = {
        (uint8_t)MidiCin::NOTE_OFF,
        (uint8_t)(0x80 | (channel & 0x0F)),
        (uint8_t)(note & 0x7F),
        (uint8_t)(velocity & 0x7F)
    };
    return sendMidiMessage(message, 4);
}

bool UsbMidi::controlChange(uint8_t channel, uint8_t controller, uint8_t value)
{
    uint8_t message[4] = {
        (uint8_t)MidiCin::CONTROL_CHANGE,
        (uint8_t)(0xB0 | (channel & 0x0F)),
        (uint8_t)(controller & 0x7F),
        (uint8_t)(value & 0x7F)
    };
    return sendMidiMessage(message, 4);
}

bool UsbMidi::programChange(uint8_t channel, uint8_t program)
{
    uint8_t message[4] = {
        (uint8_t)MidiCin::PROGRAM_CHANGE,
        (uint8_t)(0xC0 | (channel & 0x0F)),
        (uint8_t)(program & 0x7F),
        0
    };
    return sendMidiMessage(message, 4);
}

size_t UsbMidi::getQueueAvailableSize() const
{
    if (!_midiOutQueue) {
        return 0;
    }
    return uxQueueSpacesAvailable(_midiOutQueue);
}

void UsbMidi::onDeviceConnected(void (*callback)())
{
    _deviceConnectedCallback = callback;
}

void UsbMidi::onDeviceDisconnected(void (*callback)())
{
    _deviceDisconnectedCallback = callback;
}

/* ── Static callbacks → instance dispatch ───────────────────────── */

void UsbMidi::_clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg)
{
    if (arg == nullptr) return;
    UsbMidi* instance = static_cast<UsbMidi*>(arg);
    instance->_handleClientEvent(eventMsg);
}

void UsbMidi::_handleClientEvent(const usb_host_client_event_msg_t* eventMsg)
{
    esp_err_t err;
    switch (eventMsg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            if (_deviceHandle) {
                return;
            }

            err = usb_host_device_open(_clientHandle, eventMsg->new_dev.address, &_deviceHandle);
            if (err != ESP_OK) {
                return;
            }

            {
                const usb_config_desc_t* configDesc;
                err = usb_host_get_active_config_descriptor(_deviceHandle, &configDesc);
                if (err == ESP_OK) {
                    _parseConfigDescriptor(configDesc);
                }

                /* Close device if no usable MIDI interface was found */
                if (!_isMidiInterfaceFound) {
                    usb_host_device_close(_clientHandle, _deviceHandle);
                    _deviceHandle = nullptr;
                }
            }
            break;

        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            if (eventMsg->dev_gone.dev_hdl == _deviceHandle) {
                _releaseDeviceResources();
                if (_deviceDisconnectedCallback) {
                    _deviceDisconnectedCallback();
                }
            }
            break;

        default:
            break;
    }
}

/* ── Descriptor parsing ─────────────────────────────────────────── */

void UsbMidi::_parseConfigDescriptor(const usb_config_desc_t* configDesc)
{
    /*
     * Use portable pointer arithmetic rather than accessing the
     * val[] member, whose layout varies across ESP-IDF versions
     * (union at offset 0 vs. flexible array at end of struct).
     */
    const uint8_t* start = reinterpret_cast<const uint8_t*>(configDesc);
    const uint8_t* end   = start + configDesc->wTotalLength;
    const uint8_t* p     = start + configDesc->bLength; /* skip config header */

    /*
     * Track whether we are currently inside the MIDI interface's
     * descriptor block.  A new interface descriptor always ends the
     * previous interface's endpoint zone.
     */
    bool inMidiInterface = false;

    while (p < end) {
        const uint8_t bLength = p[0];
        if (bLength < USB_DESC_MIN_LENGTH || (p + bLength) > end) break;

        const uint8_t bDescriptorType = p[1];
        switch (bDescriptorType) {
            case USB_B_DESCRIPTOR_TYPE_INTERFACE:
                if (bLength < USB_INTF_DESC_MIN_LENGTH) break;
                if (!_isMidiInterfaceFound) {
                    _findAndClaimMidiInterface(reinterpret_cast<const usb_intf_desc_t*>(p));
                    inMidiInterface = _isMidiInterfaceFound;
                } else {
                    /* New interface descriptor → no longer in MIDI zone */
                    inMidiInterface = false;
                }
                break;

            case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
                if (bLength < USB_EP_DESC_MIN_LENGTH) break;
                if (inMidiInterface && !_areEndpointsReady.load(std::memory_order_relaxed)) {
                    _setupMidiEndpoints(reinterpret_cast<const usb_ep_desc_t*>(p));
                }
                break;

            default:
                break;
        }
        p += bLength;
    }
}

void UsbMidi::_findAndClaimMidiInterface(const usb_intf_desc_t* intf)
{
    if (intf->bInterfaceClass    == USB_CLASS_AUDIO &&
        intf->bInterfaceSubClass == USB_AUDIO_SUBCLASS_MIDI_STREAMING)
    {
        esp_err_t err = usb_host_interface_claim(
            _clientHandle, _deviceHandle,
            intf->bInterfaceNumber, intf->bAlternateSetting);

        if (err == ESP_OK) {
            _midiInterfaceNumber = intf->bInterfaceNumber;
            _isMidiInterfaceFound = true;
        } else {
            _isMidiInterfaceFound = false;
        }
    }
}

/* ── Endpoint setup ─────────────────────────────────────────────── */

void UsbMidi::_setupMidiEndpoints(const usb_ep_desc_t* endpoint)
{
    if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK)
        != USB_BM_ATTRIBUTES_XFER_BULK) {
        return;
    }

    if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
        _setupMidiInEndpoint(endpoint);
    } else {
        _setupMidiOutEndpoint(endpoint);
    }

    if (!_areEndpointsReady.load(std::memory_order_relaxed)
        && _midiOutTransfer && _midiInTransfers[0])
    {
        _areEndpointsReady.store(true, std::memory_order_release);
        if (_deviceConnectedCallback) _deviceConnectedCallback();
    }
}

void UsbMidi::_setupMidiInEndpoint(const usb_ep_desc_t* endpoint)
{
    for (int i = 0; i < NUM_MIDI_IN_TRANSFERS; i++) {
        if (_midiInTransfers[i] != nullptr) continue;

        esp_err_t err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &_midiInTransfers[i]);
        if (err == ESP_OK) {
            _midiInTransfers[i]->device_handle    = _deviceHandle;
            _midiInTransfers[i]->bEndpointAddress  = endpoint->bEndpointAddress;
            _midiInTransfers[i]->callback          = _midiTransferCallback;
            _midiInTransfers[i]->context           = this;
            _midiInTransfers[i]->num_bytes         = endpoint->wMaxPacketSize;

            _midiInEpAddr = endpoint->bEndpointAddress;
            _midiInErrorCount[i] = 0;

            err = usb_host_transfer_submit(_midiInTransfers[i]);
            if (err != ESP_OK) {
                usb_host_transfer_free(_midiInTransfers[i]);
                _midiInTransfers[i] = nullptr;
            }
        } else {
            _midiInTransfers[i] = nullptr;
        }
    }
}

void UsbMidi::_setupMidiOutEndpoint(const usb_ep_desc_t* endpoint)
{
    if (_midiOutTransfer != nullptr) return;

    esp_err_t err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &_midiOutTransfer);
    if (err == ESP_OK) {
        _midiOutTransfer->device_handle   = _deviceHandle;
        _midiOutTransfer->bEndpointAddress = endpoint->bEndpointAddress;
        _midiOutTransfer->callback         = _midiTransferCallback;
        _midiOutTransfer->context          = this;

        _midiOutEpAddr = endpoint->bEndpointAddress;
    } else {
        _midiOutTransfer = nullptr;
    }
}

/* ── Transfer callback ──────────────────────────────────────────── */

void UsbMidi::_midiTransferCallback(usb_transfer_t* transfer)
{
    if (transfer == nullptr || transfer->context == nullptr) return;
    UsbMidi* instance = static_cast<UsbMidi*>(transfer->context);
    instance->_handleMidiTransfer(transfer);
}

void UsbMidi::_handleMidiTransfer(usb_transfer_t* transfer)
{
    if (!_areEndpointsReady.load(std::memory_order_acquire)
        || _deviceHandle != transfer->device_handle)
    {
        return;
    }

    bool isInTransfer = (transfer->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK);

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        if (isInTransfer) {
            /* Find transfer slot index */
            int idx = -1;
            for (int i = 0; i < NUM_MIDI_IN_TRANSFERS; ++i) {
                if (_midiInTransfers[i] == transfer) { idx = i; break; }
            }

            if (idx >= 0) {
                _midiInErrorCount[idx] = 0;
            }

            int validBytes = transfer->actual_num_bytes & ~3;
            for (int i = 0; i < validBytes; i += 4) {
                uint8_t* const p = &transfer->data_buffer[i];
                uint8_t codeIndexNumber = p[0] & 0x0F;

                if (codeIndexNumber != 0 && _midiMessageCallback) {
                    _midiMessageCallback(*reinterpret_cast<const uint8_t (*)[4]>(p));
                }
            }

            /*
             * Defer re-submission to update().  This ensures that a
             * DEV_GONE event arriving in the same event batch is
             * processed first, preventing usb_host_transfer_submit
             * from being called on a disconnected device.
             */
            if (idx >= 0) {
                _pendingInResubmit[idx] = true;
            }
        } else {
            _isMidiOutBusy.store(false, std::memory_order_release);
            _processMidiOutQueue();
        }
    } else if (transfer->status != USB_TRANSFER_STATUS_CANCELED) {
        if (isInTransfer) {
            /* Re-submit only if error count is within limit */
            int idx = -1;
            for (int i = 0; i < NUM_MIDI_IN_TRANSFERS; ++i) {
                if (_midiInTransfers[i] == transfer) { idx = i; break; }
            }
            if (idx >= 0 && _midiInErrorCount[idx] < MIDI_IN_MAX_ERROR_COUNT) {
                _midiInErrorCount[idx]++;
                _pendingInResubmit[idx] = true;
            }
            /* else: give up re-submitting this transfer */
        } else {
            _isMidiOutBusy.store(false, std::memory_order_release);
        }
    }
}

/* ── OUT queue processing ───────────────────────────────────────── */

void UsbMidi::_processMidiOutQueue()
{
    if (!_areEndpointsReady.load(std::memory_order_acquire)
        || !_midiOutTransfer || !_midiOutQueue)
    {
        return;
    }

    bool expected = false;
    if (!_isMidiOutBusy.compare_exchange_strong(expected, true,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
        return;
    }

    if (uxQueueMessagesWaiting(_midiOutQueue) == 0) {
        _isMidiOutBusy.store(false, std::memory_order_release);
        return;
    }

    size_t bytesToSend = 0;
    uint8_t tempMessage[4];
    size_t maxPayload = _midiOutTransfer->data_buffer_size;

    while (bytesToSend + 4 <= maxPayload && uxQueueMessagesWaiting(_midiOutQueue) > 0) {
        if (xQueueReceive(_midiOutQueue, tempMessage, 0) == pdPASS) {
            memcpy(_midiOutTransfer->data_buffer + bytesToSend, tempMessage, 4);
            bytesToSend += 4;
        } else {
            break;
        }
    }

    if (bytesToSend > 0) {
        _midiOutTransfer->num_bytes = bytesToSend;
        if (usb_host_transfer_submit(_midiOutTransfer) == ESP_OK) {
            _midiOutSubmitTime = (uint32_t)millis();
        } else {
            _isMidiOutBusy.store(false, std::memory_order_release);
        }
    } else {
        _isMidiOutBusy.store(false, std::memory_order_release);
    }
}

/* ── OUT transfer timeout watchdog ──────────────────────────────── */

void UsbMidi::_checkOutTransferTimeout()
{
    if (!_isMidiOutBusy.load(std::memory_order_acquire)) return;
    if (!_midiOutTransfer) return;

    uint32_t elapsed = (uint32_t)millis() - _midiOutSubmitTime;
    if (elapsed >= MIDI_OUT_TIMEOUT_MS) {
        /*
         * The OUT transfer appears stuck.  Halt and flush the
         * endpoint, then release the busy flag so the queue can
         * resume on the next cycle.
         */
        if (_midiOutEpAddr != 0 && _deviceHandle) {
            usb_host_endpoint_halt(_deviceHandle, _midiOutEpAddr);
            usb_host_endpoint_flush(_deviceHandle, _midiOutEpAddr);
            usb_host_endpoint_clear(_deviceHandle, _midiOutEpAddr);
        }
        _isMidiOutBusy.store(false, std::memory_order_release);
    }
}

/* ── Deferred IN transfer re-submission ─────────────────────────── */

void UsbMidi::_resubmitPendingInTransfers()
{
    if (!_areEndpointsReady.load(std::memory_order_acquire)) {
        /* Device disconnected — clear all pending flags, do not submit */
        for (int i = 0; i < NUM_MIDI_IN_TRANSFERS; ++i) {
            _pendingInResubmit[i] = false;
        }
        return;
    }

    for (int i = 0; i < NUM_MIDI_IN_TRANSFERS; ++i) {
        if (!_pendingInResubmit[i] || !_midiInTransfers[i]) continue;
        _pendingInResubmit[i] = false;

        if (usb_host_transfer_submit(_midiInTransfers[i]) != ESP_OK) {
            _areEndpointsReady.store(false, std::memory_order_release);
            return;
        }
    }
}

/* ── Resource cleanup ───────────────────────────────────────────── */

void UsbMidi::_cancelInFlightTransfers()
{
    if (!_deviceHandle || !_isMidiInterfaceFound) return;

    if (_midiInEpAddr != 0) {
        usb_host_endpoint_halt(_deviceHandle, _midiInEpAddr);
        usb_host_endpoint_flush(_deviceHandle, _midiInEpAddr);
    }
    if (_midiOutEpAddr != 0) {
        usb_host_endpoint_halt(_deviceHandle, _midiOutEpAddr);
        usb_host_endpoint_flush(_deviceHandle, _midiOutEpAddr);
    }
}

void UsbMidi::_releaseDeviceResources()
{
    if (!_deviceHandle) return;

    _areEndpointsReady.store(false, std::memory_order_release);
    _isMidiOutBusy.store(false, std::memory_order_release);

    if (_midiOutQueue) {
        xQueueReset(_midiOutQueue);
    }

    _cancelInFlightTransfers();

    for (int i = 0; i < NUM_MIDI_IN_TRANSFERS; ++i) {
        if (_midiInTransfers[i]) {
            usb_host_transfer_free(_midiInTransfers[i]);
            _midiInTransfers[i] = nullptr;
        }
        _midiInErrorCount[i] = 0;
        _pendingInResubmit[i] = false;
    }
    if (_midiOutTransfer) {
        usb_host_transfer_free(_midiOutTransfer);
        _midiOutTransfer = nullptr;
    }

    if (_isMidiInterfaceFound) {
        usb_host_interface_release(_clientHandle, _deviceHandle, _midiInterfaceNumber);
    }
    usb_host_device_close(_clientHandle, _deviceHandle);

    _deviceHandle = nullptr;
    _isMidiInterfaceFound = false;
    _midiInEpAddr = 0;
    _midiOutEpAddr = 0;
    _midiOutSubmitTime = 0;
}
