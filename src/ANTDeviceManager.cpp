#include <ANTDeviceManager.h>
#include <Config.h>
#include <Utils.h>

bool ANTDeviceManager::started = false;
ArduinoSerialAnt ANTDeviceManager::ant;

bool ANTDeviceManager::start() {
  if (!started) {
    ResetSystem rs;
    RequestMessage rm = RequestMessage();

    Serial2.setPins(ANT_SERIAL_PIN_RXD, ANT_SERIAL_PIN_TXD, -1, -1);
    Serial2.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
    Serial2.begin(ANT_BAUD_RATE);
    ant.setSerial(Serial2);
    ant.send(rs);
    log_i("Running");

    parseMessage();

    sleep(3); 
    rm.setRequestedMessage(ANT_VERSION);
    ant.send(rm);
    parseMessage();

    rm.setRequestedMessage(CAPABILITIES);
    ant.send(rm);
    parseMessage();

    rm.setRequestedMessage(ADVANCED_BURST_CAPABILITES);
    ant.send(rm);
    parseMessage();
    started = true;
    return true;
  }
  return false;
}

void ANTDeviceManager::stop() {
  started = false;
}

void ANTDeviceManager::update() {
  if (started) {
    parseMessage();
  }
}

void ANTDeviceManager::parseMessage() {
    ant.readPacket();
    if (ant.getResponse().isAvailable()) {
        uint8_t msgId = ant.getResponse().getMsgId();
        switch (msgId) {
            case CHANNEL_EVENT:
            {
                ChannelEventResponse cer = ChannelEventResponse();
                ant.getResponse().getChannelEventResponseMsg(cer);
                log_i("Received Msg: ChannelEventResponse");
                log_i("Channel: %d", cer.getChannelNumber());
                parseEventMessage(cer.getCode());
                break;
            }

            case ANT_VERSION:
            {
                AntVersion av = AntVersion();
                uint64_t version = 0;
                ant.getResponse().getAntVersionMsg(av);
                log_i("Version: ");
                for (uint8_t i = 0; i < av.getPacketLength(); i++)
                {
                    version += (((uint64_t)av.getVersionByte(i)) << i*8);
                }

                uint64_t xx = version/1000000000ULL;
                if (xx >0) log_i("%ld", (long)xx);
                log_i("%ld", (long)(version-xx*1000000000));

                log_i("");
                break;
            }

            case START_UP_MESSAGE:
            {
                StartUpMessage sum = StartUpMessage();
                ant.getResponse().getStartUpMsg(sum);
                log_i("Received Msg: StartupMessage");
                log_i("Message length: %d", sum.getLength());
                break;
            }

            case CAPABILITIES:
            {
                Capabilities cap = Capabilities();
                ant.getResponse().getCapabilitiesMsg(cap);

                log_i("Max Channels: %d", cap.getMaxChannels());
                log_i("Max Networks: %d", cap.getMaxNetworks());

                log_i("Standard Options:");
                if ( cap.getStandardOptions() & CAPABILITY_NO_RECEIVE_CHANNELS  )
                {
                    log_i("No Receive Channels");
                }
                if ( cap.getStandardOptions() & CAPABILITY_NO_TRANSMIT_CHANNELS )
                {
                    log_i("No Transmit Channels");
                }
                if ( cap.getStandardOptions() & CAPABILITY_NO_RECEIVE_MESSAGES  )
                {
                    log_i("No Receive Messages");
                }
                if ( cap.getStandardOptions() & CAPABILITY_NO_TRANSMIT_MESSAGES )
                {
                    log_i("No Transmit Messages");
                }
                if ( cap.getStandardOptions() & CAPABILITY_NO_ACKD_MESSAGES     )
                {
                    log_i("No Ackd Messages");
                }
                if ( cap.getStandardOptions() & CAPABILITY_NO_BURST_MESSAGES    )
                {
                    log_i("No Burst Messages");
                }

                log_i("Advanced Options:");
                if ( cap.getAdvancedOptions(0) & CAPABILITY_NETWORK_ENABLED              )
                {
                    log_i("Network Enabled");
                }
                if ( cap.getAdvancedOptions(0) & CAPABILITY_SERIAL_NUMBER_ENABLED        )
                {
                    log_i("Serial Number Enabled");
                }
                if ( cap.getAdvancedOptions(0) & CAPABILITY_PER_CHANNEL_TX_POWER_ENABLED )
                {
                    log_i("Per Channel Tx Power Enabled");
                }
                if ( cap.getAdvancedOptions(0) & CAPABILITY_LOW_PRIORITY_SEARCH_ENABLED  )
                {
                    log_i("Low Priority Search Enabled");
                }
                if ( cap.getAdvancedOptions(0) & CAPABILITY_SCRIPT_ENABLED               )
                {
                    log_i("Script Enabled");
                }
                if ( cap.getAdvancedOptions(0) & CAPABILITY_SEARCH_LIST_ENABLED          )
                {
                    log_i("Search List Enabled");
                }

                log_i("Advanced Options 2:");
                if ( cap.getAdvancedOptions(1) & CAPABILITY_LED_ENABLED         )
                {
                    log_i("LED Enabled");
                }
                if ( cap.getAdvancedOptions(1) & CAPABILITY_EXT_MESSAGE_ENABLED )
                {
                    log_i("Ext Message Enabled");
                }
                if ( cap.getAdvancedOptions(1) & CAPABILITY_SCAN_MODE_ENABLED   )
                {
                    log_i("Scan Mode Enabled");
                }
                if ( cap.getAdvancedOptions(1) & CAPABILITY_PROX_SEARCH_ENABLED )
                {
                    log_i("Prox Search Enabled");
                }
                if ( cap.getAdvancedOptions(1) & CAPABILITY_EXT_ASSIGN_ENABLED  )
                {
                    log_i("Ext Assign Enabled");
                }
                if ( cap.getAdvancedOptions(1) & CAPABILITY_FS_ANTFS_ENABLED    )
                {
                    log_i("FS ANTFS Enabled");
                }
                if ( cap.getAdvancedOptions(1) & CAPABILITY_FIT1_ENABLED        )
                {
                    log_i("FIT1 Enabled");
                }

                log_i("Advanced Options 3:");
                if ( cap.getAdvancedOptions(2) & CAPABILITY_ADVANCED_BURST_ENABLED         )
                {
                    log_i("Advanced Burst Enabled");
                }
                if ( cap.getAdvancedOptions(2) & CAPABILITY_EVENT_BUFFERING_ENABLED        )
                {
                    log_i("Event Buffering Enabled");
                }
                if ( cap.getAdvancedOptions(2) & CAPABILITY_EVENT_FILTERING_ENABLED        )
                {
                    log_i("Event Filtering Enabled");
                }
                if ( cap.getAdvancedOptions(2) & CAPABILITY_HIGH_DUTY_SEARCH_ENABLED       )
                {
                    log_i("High Duty Search Enabled");
                }
                if ( cap.getAdvancedOptions(2) & CAPABILITY_SEARCH_SHARING_ENABLED         )
                {
                    log_i("Search Sharing Enabled");
                }
                if ( cap.getAdvancedOptions(2) & CAPABILITY_SELECTIVE_DATA_UPDATES_ENABLED )
                {
                    log_i("Selective Data Updates Enabled");
                }
                if ( cap.getAdvancedOptions(2) & CAPABILITY_ENCRYPTED_CHANNEL_ENABLED      )
                {
                    log_i("Encrypted Channel Enabled");
                }

                log_i("Advanced Options 4:");
                if ( cap.getAdvancedOptions(3) & CAPABILITY_RFACTIVE_NOTIFICATION_ENABLED )
                {
                    log_i("RfActive Notification Enabled");
                }
                break;
            }

            case ADVANCED_BURST_CAPABILITES:
            {
                uint8_t packet_length = 0;
                AdvancedBurstCapabilitiesConfiguration abc = AdvancedBurstCapabilitiesConfiguration();
                ant.getResponse().getAdvancedBurstCapabilitiesConfigurationMsg(abc);
                log_i("Supported Max Packet Length:");
                packet_length = abc.getSupportedMaxPacketLength();
                switch ( packet_length )
                {
                    case ADV_BURST_MAX_PACKET_LENGTH_8_BYTE:
                        log_i("8-byte");
                        break;

                    case ADV_BURST_MAX_PACKET_LENGTH_16_BYTE:
                        log_i("16-byte");
                        break;

                    case ADV_BURST_MAX_PACKET_LENGTH_24_BYTE:
                        log_i("24-byte");
                        break;
                }

                log_i("Supported Features:");
                if ( abc.getSupportedFeatures() & ADV_BURST_FREQUENCY_HOP_ENABLED )
                {
                    log_i("Frequency Hop Enabled");
                }
                break;
            }

            default:
                log_i("Undefined Message: ", msgId);
                break;
        }
    }
    else if (ant.getResponse().isError())
    {
        log_i("ANT MSG ERROR: ", ant.getResponse().getErrorCode());
    }
}


void ANTDeviceManager::parseEventMessage(uint8_t code)
{
    log_i("Code: ");
    switch (code)
    {
        case STATUS_RESPONSE_NO_ERROR:
            log_i("RESPONSE_NO_ERROR");
            break;

        case STATUS_EVENT_CHANNEL_CLOSED:
            log_i("EVENT_CHANNEL_CLOSED");
            break;

        case STATUS_EVENT_TX:
            log_i("EVENT_TX");
            break;

        case STATUS_INVALID_MESSAGE:
            log_i("INVALID_MESSAGE");
            break;

        default:
            log_i("Code: ", code);
            break;
    }
}
