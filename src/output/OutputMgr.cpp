/*
* OutputMgr.cpp - Output Management class
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2020 Shelby Merrick
* http://www.forkineye.com
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*   This is a factory class used to manage the output port. It creates and deletes
*   the output channel functionality as needed to support any new configurations 
*   that get sent from from the WebPage.
*
*/

#include "../ESPixelStick.h"
#include "../FileIO.h"

//-----------------------------------------------------------------------------
// bring in driver definitions
#include "OutputDisabled.hpp"
#include "OutputGECE.hpp"
#include "OutputSerial.hpp"
#include "OutputWS2811.hpp"
#include "OutputRelay.hpp"
// needs to be last
#include "OutputMgr.hpp"

#include "../input/InputMgr.hpp"

//-----------------------------------------------------------------------------
// Local Data definitions
//-----------------------------------------------------------------------------
typedef struct 
{
    c_OutputMgr::e_OutputType id;
    String name;
}OutputTypeXlateMap_t;

OutputTypeXlateMap_t OutputTypeXlateMap[c_OutputMgr::e_OutputType::OutputType_End] =
{
    {c_OutputMgr::e_OutputType::OutputType_WS2811,   "WS2811"   },
    {c_OutputMgr::e_OutputType::OutputType_GECE,     "GECE"     },
    {c_OutputMgr::e_OutputType::OutputType_Serial,   "Serial"   },
    {c_OutputMgr::e_OutputType::OutputType_Renard,   "Renard"   },
    {c_OutputMgr::e_OutputType::OutputType_DMX,      "DMX"      },
    {c_OutputMgr::e_OutputType::OutputType_Relay,    "Relay"    },
    {c_OutputMgr::e_OutputType::OutputType_Disabled, "Disabled" }
};

//-----------------------------------------------------------------------------
typedef struct 
{
    gpio_num_t dataPin;
    uart_port_t UartId;
}OutputChannelIdToGpioAndPortEntry_t;

//-----------------------------------------------------------------------------
OutputChannelIdToGpioAndPortEntry_t OutputChannelIdToGpioAndPort[] =
{
    {gpio_num_t::GPIO_NUM_2,  uart_port_t::UART_NUM_1},
#ifdef ARDUINO_ARCH_ESP32
    {gpio_num_t::GPIO_NUM_13, uart_port_t::UART_NUM_2},
#endif // def ARDUINO_ARCH_ESP32
    {gpio_num_t::GPIO_NUM_10, uart_port_t (-1)},
};

//-----------------------------------------------------------------------------
// Methods
//-----------------------------------------------------------------------------
///< Start up the driver and put it into a safe mode
c_OutputMgr::c_OutputMgr ()
{
    ConfigFileName = String (F ("/")) + String (OM_SECTION_NAME) + F(".json");

    // clear the input data buffer
    memset ((char*)&OutputBuffer[0], 0, sizeof (OutputBuffer));

    // this gets called pre-setup so there is nothing we can do here.
    int pOutputChannelDriversIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        pOutputChannelDrivers[pOutputChannelDriversIndex++] = nullptr;
    }

} // c_OutputMgr

//-----------------------------------------------------------------------------
///< deallocate any resources and put the output channels into a safe state
c_OutputMgr::~c_OutputMgr()
{
    // DEBUG_START;

    // delete pOutputInstances;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        // the drivers will put the hardware in a safe state
        delete CurrentOutput;
    }
    // DEBUG_END;

} // ~c_OutputMgr

//-----------------------------------------------------------------------------
///< Start the module
void c_OutputMgr::Begin ()
{
    // DEBUG_START;

    // prevent recalls
    if (true == HasBeenInitialized) { return; }
    HasBeenInitialized = true;

    // make sure the pointers are set up properly
    int ChannelIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        InstantiateNewOutputChannel (e_OutputChannelIds(ChannelIndex++),
                                     e_OutputType::OutputType_Disabled);
        // DEBUG_V ("");
    }

    // load up the configuration from the saved file. This also starts the drivers
    LoadConfig ();

    // CreateNewConfig ();

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
void c_OutputMgr::CreateJsonConfig (JsonObject& jsonConfig)
{
    // DEBUG_START;

    // extern void PrettyPrint (JsonObject&, String);
    // DEBUG_V ("");
    // PrettyPrint (jsonConfig, String ("jsonConfig"));

    // add OM config parameters
    // DEBUG_V ("");

    // add the channels header
    JsonObject OutputMgrChannelsData;
    if (true == jsonConfig.containsKey (OM_CHANNEL_SECTION_NAME))
    {
        // DEBUG_V ("");
        OutputMgrChannelsData = jsonConfig[OM_CHANNEL_SECTION_NAME];
    }
    else
    {
        // add our section header
        // DEBUG_V ("");
        OutputMgrChannelsData = jsonConfig.createNestedObject (OM_CHANNEL_SECTION_NAME);
    }

    // add the channel configurations
    // DEBUG_V ("For Each Output Channel");
    for (auto CurrentChannel : pOutputChannelDrivers)
    {
        // DEBUG_V (String("Create Section in Config file for the output channel: '") + CurrentChannel->GetOutputChannelId() + "'");
        // create a record for this channel
        JsonObject ChannelConfigData;
        String sChannelId = String (CurrentChannel->GetOutputChannelId ());
        if (true == OutputMgrChannelsData.containsKey (sChannelId))
        {
            // DEBUG_V ("");
            ChannelConfigData = OutputMgrChannelsData[sChannelId];
        }
        else
        {
            // add our section header
            // DEBUG_V ("");
            ChannelConfigData = OutputMgrChannelsData.createNestedObject (sChannelId);
        }

        // save the name as the selected channel type
        ChannelConfigData[OM_CHANNEL_TYPE_NAME] = int (CurrentChannel->GetOutputType ());

        String DriverTypeId = String (int (CurrentChannel->GetOutputType ()));
        JsonObject ChannelConfigByTypeData;
        if (true == ChannelConfigData.containsKey (String (DriverTypeId)))
        {
            ChannelConfigByTypeData = ChannelConfigData[DriverTypeId];
            // DEBUG_V ("");
        }
        else
        {
            // add our section header
            // DEBUG_V ("");
            ChannelConfigByTypeData = ChannelConfigData.createNestedObject (DriverTypeId);
        }

        // DEBUG_V ("");
        // PrettyPrint (ChannelConfigByTypeData, String ("jsonConfig"));

        // ask the channel to add its data to the record
        // DEBUG_V ("Add the output channel configuration for type: " + DriverTypeId);

        // Populate the driver name
        String DriverName = ""; 
        CurrentChannel->GetDriverName (DriverName);
        // DEBUG_V (String ("DriverName: ") + DriverName);

        ChannelConfigByTypeData[F ("type")] = DriverName;

        // DEBUG_V ("");
        // PrettyPrint (ChannelConfigByTypeData, String ("jsonConfig"));
        // DEBUG_V ("");

        CurrentChannel->GetConfig (ChannelConfigByTypeData);

        // DEBUG_V ("");
        // PrettyPrint (ChannelConfigByTypeData, String ("jsonConfig"));
        // DEBUG_V ("");

    } // for each output channel

    // DEBUG_V ("");
    // PrettyPrint (jsonConfig, String ("jsonConfig"));

    // smile. Your done
    // DEBUG_END;
} // CreateJsonConfig

//-----------------------------------------------------------------------------
/*
    The running config is made from a composite of running and not instantiated
    objects. To create a complete config we need to start each output type on
    each output channel and collect the configuration at each stage.
*/
void c_OutputMgr::CreateNewConfig ()
{
    // DEBUG_START;
    LOG_PORT.println (F ("--- WARNING: Creating a new Output Manager configuration Data set - Start ---"));

    // create a place to save the config
    DynamicJsonDocument JsonConfigDoc (OM_MAX_CONFIG_SIZE);
    JsonObject JsonConfig = JsonConfigDoc.createNestedObject (OM_SECTION_NAME);

    // DEBUG_V ("for each output type");
    for (int outputTypeId = int (OutputType_Start); 
         outputTypeId < int (OutputType_End); 
         ++outputTypeId)
    {
        // DEBUG_V ("for each interface");
        int ChannelIndex = 0;
        for (auto CurrentOutput : pOutputChannelDrivers)
        {
            // DEBUG_V (String("instantiate output type: ") + String(outputTypeId));
            InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex++), e_OutputType (outputTypeId));
        }// end for each interface

        // DEBUG_V ("collect the config data for this output type");
        CreateJsonConfig (JsonConfig);

        // DEBUG_V ("");

    } // end for each output type

    // DEBUG_V ("leave the outputs disabled");
    int ChannelIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex), e_OutputType::OutputType_Disabled);
    }// end for each interface
    // DEBUG_V ("");
    CreateJsonConfig (JsonConfig);

    ConfigData.clear ();

    // DEBUG_V ("");
    serializeJson (JsonConfigDoc, ConfigData);
    // DEBUG_V (String("ConfigData: ") + ConfigData);

    ConfigSaveNeeded = false;
    SaveConfig ();
    // DEBUG_V (String ("ConfigData: ") + ConfigData);

    JsonConfigDoc.garbageCollect ();

    LOG_PORT.println (F ("--- WARNING: Creating a new Output Manager configuration Data set - Done ---"));
    // DEBUG_END;

} // CreateNewConfig

//-----------------------------------------------------------------------------
void c_OutputMgr::GetConfig (char * Response )
{
    // DEBUG_START;

    strcat (Response, ConfigData.c_str ());

    // DEBUG_END;

} // GetConfig

//-----------------------------------------------------------------------------
void c_OutputMgr::GetPortConfig (e_OutputChannelIds portId, String & ConfigResponse)
{
    // DEBUG_START;

// try to load and process the config file
    if (!FileIO::loadConfig (ConfigFileName, [this, portId, &ConfigResponse](DynamicJsonDocument& JsonConfigDoc)
        {
            // DEBUG_V ("");
            JsonObject JsonConfig = JsonConfigDoc.as<JsonObject> ();
            // DEBUG_V ("");

            do // once
            {
                if (false == JsonConfig.containsKey (OM_SECTION_NAME))
                {
                    LOG_PORT.println (F ("No Output Interface Settings Found."));
                    break;
                }
                JsonObject OutputChannelMgrData = JsonConfig[OM_SECTION_NAME];
                // DEBUG_V ("");

                // do we have a channel configuration array?
                if (false == OutputChannelMgrData.containsKey (OM_CHANNEL_SECTION_NAME))
                {
                    // if not, flag an error and stop processing
                    LOG_PORT.println (F ("No Output Channel Settings Found."));
                    break;
                }
                JsonObject OutputChannelArray = OutputChannelMgrData[OM_CHANNEL_SECTION_NAME];
                // DEBUG_V ("");

                // get access to the channel config
                if (false == OutputChannelArray.containsKey (String (portId).c_str ()))
                {
                    // if not, flag an error and stop processing
                    LOG_PORT.println (String (F ("No Output Settings Found for Channel '")) + portId + String (F ("'.")));
                    break;
                }
                JsonObject OutputChannelConfig = OutputChannelArray[String (portId).c_str ()];
                // DEBUG_V ("");

                // set a default value for channel type
                uint32_t ChannelType = uint32_t (OutputType_End);
                FileIO::setFromJSON (ChannelType, OutputChannelConfig[OM_CHANNEL_TYPE_NAME]);
                // DEBUG_V ("");

                // is it a valid / supported channel type
                if ((ChannelType < uint32_t (OutputType_Start)) || (ChannelType >= uint32_t (OutputType_End)))
                {
                    // if not, flag an error and move on to the next channel
                    break;
                }
                // DEBUG_V ("");

                // do we have a configuration for the channel type?
                if (false == OutputChannelConfig.containsKey (String (ChannelType)))
                {
                    // if not, flag an error and stop processing
                    LOG_PORT.println (String (F ("No Output Settings Found for Channel '")) + portId + String (F ("'.")));
                    continue;
                }

                JsonObject OutputChannelDriverConfig = OutputChannelConfig[String (ChannelType)];
                // DEBUG_V ("");

                serializeJson (OutputChannelDriverConfig, ConfigResponse);
                // DEBUG_V (String("ConfigResponse: ") + ConfigResponse);

            } while (false);

            // DEBUG_V ("");
        }, OM_MAX_CONFIG_SIZE))
    {
        LOG_PORT.println (F ("EEEE Error loading Output Manager Config File. EEEE"));
    }
        
   // DEBUG_END;

} // GetPortConfigs

//-----------------------------------------------------------------------------
void c_OutputMgr::GetOptions (JsonObject & jsonOptions)
{
    // extern void PrettyPrint (JsonObject&, String);
    // DEBUG_START;
    JsonArray jsonChannelsArray = jsonOptions.createNestedArray (OM_CHANNEL_SECTION_NAME);
    // DEBUG_V (String("ConfigData: ") + ConfigData);

    DynamicJsonDocument OutputConfigData(OM_MAX_CONFIG_SIZE);
    DeserializationError deError = deserializeJson (OutputConfigData, ConfigData);
    // DEBUG_V (String("deError: ") + String(deError.c_str()));

    // JsonObject OC_info = OutputConfigData["output_config"];
    // PrettyPrint (OC_info, String ("OC_info"));

    JsonObject OutputChannelConfigDataArray = OutputConfigData["output_config"]["channels"];
    // PrettyPrint (OutputChannelConfigDataArray, String ("OutputChannelConfigDataArray"));
    // DEBUG_V ("");

    // build a list of the current available channels and their output type
    for (c_OutputCommon * currentOutput : pOutputChannelDrivers)
    {
        e_OutputChannelIds ChannelId = currentOutput->GetOutputChannelId ();
        JsonObject OutputChannelConfigData = OutputChannelConfigDataArray[String(ChannelId)];
        // DEBUG_V ("");
        // PrettyPrint (OutputChannelConfigData, String("OutputChannelConfigData"));

        JsonObject channelOptionData = jsonChannelsArray.createNestedObject ();
        channelOptionData[F ("id")]             = ChannelId;
        channelOptionData[F ("selectedoption")] = currentOutput->GetOutputType ();

        // DEBUG_V ("");
        JsonArray jsonOptionsArray = channelOptionData.createNestedArray (F ("list"));

        // Build a list of Valid options for this device
        for (int currentOutputType = OutputType_Start;
            currentOutputType < OutputType_End;
            currentOutputType++)
        {
            // DEBUG_V ("");
            if (OutputChannelConfigData.containsKey (String((int)currentOutputType)))
            {
                JsonObject CurrentOutputChannelConfigData = OutputChannelConfigData[String(currentOutputType)];
                // PrettyPrint (CurrentOutputChannelConfigData, String ("CurrentOutputChannelConfigData"));
                // DEBUG_V ("");

                JsonObject jsonOptionsArrayEntry = jsonOptionsArray.createNestedObject ();
                // DEBUG_V ("");

                String name;
                FileIO::setFromJSON (name, CurrentOutputChannelConfigData[F ("type") ]);
                // DEBUG_V (String("name: ") + name);
                // DEBUG_V (String ("id: ") + currentOutputType);

                jsonOptionsArrayEntry[F ("id")]   = currentOutputType;
                jsonOptionsArrayEntry[F ("name")] = name;
                // DEBUG_V ("");
            }
            else
            {
                // DEBUG_V ("Output Type Not in list");
            }

            // DEBUG_V ("");

        } // end for each output type
    }

    // DEBUG_END;

} // GetOptions

//-----------------------------------------------------------------------------
void c_OutputMgr::GetStatus (JsonObject & jsonStatus)
{
    // DEBUG_START;

    JsonArray OutputStatus = jsonStatus.createNestedArray (F("output"));
    uint channelIndex = 0;
    for (auto CurrentOutput : pOutputChannelDrivers)
    {
        JsonObject channelStatus = OutputStatus.createNestedObject ();
        CurrentOutput->GetStatus (channelStatus);
        channelIndex++;
        // DEBUG_V("");

    }

    // DEBUG_END;
} // GetStatus

//-----------------------------------------------------------------------------
/* Create an instance of the desired output type in the desired channel
*
* WARNING:  This function assumes there is a driver running in the identified
*           out channel. These must be set up and started when the manager is
*           started.
*
    needs
        channel ID
        channel type
    returns
        nothing
*/
void c_OutputMgr::InstantiateNewOutputChannel (e_OutputChannelIds ChannelIndex, e_OutputType NewOutputChannelType)
{
    // DEBUG_START;

    do // once
    {
        // is there an existing driver?
        if (nullptr != pOutputChannelDrivers[ChannelIndex])
        {
            // int temp = pOutputChannelDrivers[ChannelIndex]->GetOutputType ();
            // DEBUG_V (String("pOutputChannelDrivers[ChannelIndex]->GetOutputType () '") + temp + String("'"));
            // DEBUG_V (String("NewOutputChannelType '") + int(NewOutputChannelType) + "'");

            // DEBUG_V ("does the driver need to change?");
            if (pOutputChannelDrivers[ChannelIndex]->GetOutputType () == NewOutputChannelType)
            {
                // DEBUG_V ("nothing to change");
                break;
            }

            // DEBUG_V ("shut down the existing driver");
            delete pOutputChannelDrivers[ChannelIndex];
            pOutputChannelDrivers[ChannelIndex] = nullptr;
            // DEBUG_V ("");
        } // end there is an existing driver

        // DEBUG_V ("");

        // get the new data and UART info
        gpio_num_t dataPin = OutputChannelIdToGpioAndPort[ChannelIndex].dataPin;
        uart_port_t UartId = OutputChannelIdToGpioAndPort[ChannelIndex].UartId;

        switch (NewOutputChannelType)
        {
            case e_OutputType::OutputType_Disabled:
            {
                // LOG_PORT.println (String (F ("************** Disabled output type for channel '")) + ChannelIndex + "'. **************");
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }

            case e_OutputType::OutputType_DMX:
            {
                if (-1 == UartId)
                {
                    LOG_PORT.println (String (F ("************** Cannot Start DMX for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                    // DEBUG_V ("");
                }
                else
                {
                    // LOG_PORT.println (String (F ("************** Starting DMX for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputSerial (ChannelIndex, dataPin, UartId, OutputType_DMX);
                    // DEBUG_V ("");
                }
                break;
            }

            case e_OutputType::OutputType_GECE:
            {
                if (-1 == UartId)
                {
                    LOG_PORT.println (String (F ("************** Cannot Start GECE for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                    // DEBUG_V ("");
                }
                else
                {
                    // LOG_PORT.println (String (F ("************** Starting GECE for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputGECE (ChannelIndex, dataPin, UartId, OutputType_GECE);
                    // DEBUG_V ("");
                }
                break;
            }

            case e_OutputType::OutputType_Serial:
            {
                if (-1 == UartId)
                {
                    LOG_PORT.println (String (F ("************** Cannot Start Generic Serial for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                    // DEBUG_V ("");
                }
                else
                {
                    // LOG_PORT.println (String (F ("************** Starting Generic Serial for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputSerial (ChannelIndex, dataPin, UartId, OutputType_Serial);
                    // DEBUG_V ("");
                }
                break;
            }

            case e_OutputType::OutputType_Relay:
            {
                if (-1 != UartId)
                {
                    LOG_PORT.println (String (F ("************** Cannot Start RELAY for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                    // DEBUG_V ("");
                }
                else
                {
                    // LOG_PORT.println (String (F ("************** Starting RELAY for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputRelay (ChannelIndex, dataPin, UartId, OutputType_Relay);
                    // DEBUG_V ("");
                }
                break;
            }

            case e_OutputType::OutputType_Renard:
            {
                if (-1 == UartId)
                {
                    LOG_PORT.println (String (F ("************** Cannot Start Renard for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                    // DEBUG_V ("");
                }
                else
                {
                    // LOG_PORT.println (String (F ("************** Starting Renard for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputSerial (ChannelIndex, dataPin, UartId, OutputType_Renard);
                    // DEBUG_V ("");
                }
                break;
            }

            case e_OutputType::OutputType_WS2811:
            {
                if (-1 == UartId)
                {
                    LOG_PORT.println (String (F ("************** Cannot Start WS2811 for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                    // DEBUG_V ("");
                }
                else
                {
                    // LOG_PORT.println (String (F ("************** Starting WS2811 for channel '")) + ChannelIndex + "'. **************");
                    pOutputChannelDrivers[ChannelIndex] = new c_OutputWS2811 (ChannelIndex, dataPin, UartId, OutputType_WS2811);
                    // DEBUG_V ("");
                }
                break;
            }

            default:
            {
                LOG_PORT.println (String (F ("************** Unknown output type for channel '")) + ChannelIndex + "'. Using disabled. **************");
                pOutputChannelDrivers[ChannelIndex] = new c_OutputDisabled (ChannelIndex, dataPin, UartId, OutputType_Disabled);
                // DEBUG_V ("");
                break;
            }
        } // end switch (NewChannelType)

        // DEBUG_V ("");
        pOutputChannelDrivers[ChannelIndex]->Begin ();

    } while (false);

    // String temp;
    // pOutputChannelDrivers[ChannelIndex]->GetDriverName (temp);
    // DEBUG_V (String ("Driver Name: ") + temp);

    // DEBUG_END;

} // InstantiateNewOutputChannel

//-----------------------------------------------------------------------------
/* Load and process the current configuration
*
*   needs
*       Nothing
*   returns
*       Nothing
*/
void c_OutputMgr::LoadConfig ()
{
    // DEBUG_START;

    // try to load and process the config file
    if (!FileIO::loadConfig (ConfigFileName, [this](DynamicJsonDocument & JsonConfigDoc)
        {
            // DEBUG_V ("");
            JsonObject JsonConfig = JsonConfigDoc.as<JsonObject> ();
            // DEBUG_V ("");
            this->ProcessJsonConfig (JsonConfig);
            // DEBUG_V ("");
        }, OM_MAX_CONFIG_SIZE))
    {
        LOG_PORT.println (F ("EEEE Error loading Output Manager Config File. EEEE"));

        // create a config file with default values
        // DEBUG_V ("");
        CreateNewConfig ();
    }

    // DEBUG_END;

} // LoadConfig

//-----------------------------------------------------------------------------
/*
    check the contents of the config and send
    the proper portion of the config to the currently instantiated channels

    needs
        ref to data from config file
    returns
        true - config was properly processes
        false - config had an error.
*/
bool c_OutputMgr::ProcessJsonConfig (JsonObject& jsonConfig)
{
    // DEBUG_START;
    boolean Response = false;

    // save a copy of the config
    ConfigData.clear ();
    serializeJson (jsonConfig, ConfigData);
    // DEBUG_V ("");

    do // once
    {
        if (false == jsonConfig.containsKey (OM_SECTION_NAME))
        {
            LOG_PORT.println (F ("No Output Interface Settings Found. Using Defaults"));
            break;
        }
        JsonObject OutputChannelMgrData = jsonConfig[OM_SECTION_NAME];
        // DEBUG_V ("");

        // extract my own config data here

        // do we have a channel configuration array?
        if (false == OutputChannelMgrData.containsKey (OM_CHANNEL_SECTION_NAME))
        {
            // if not, flag an error and stop processing
            LOG_PORT.println (F ("No Output Channel Settings Found. Using Defaults"));
            break;
        }
        JsonObject OutputChannelArray = OutputChannelMgrData[OM_CHANNEL_SECTION_NAME];
        // DEBUG_V ("");

        // for each output channel
        for (uint32_t ChannelIndex = uint32_t (OutputChannelId_Start);
            ChannelIndex < uint32_t (OutputChannelId_End);
            ChannelIndex++)
        {
            // get access to the channel config
            if (false == OutputChannelArray.containsKey (String (ChannelIndex).c_str ()))
            {
                // if not, flag an error and stop processing
                LOG_PORT.println (String (F ("No Output Settings Found for Channel '")) + ChannelIndex + String (F ("'. Using Defaults")));
                break;
            }
            JsonObject OutputChannelConfig = OutputChannelArray[String (ChannelIndex).c_str ()];
            // DEBUG_V ("");

            // set a default value for channel type
            uint32_t ChannelType = uint32_t (OutputType_End);
            FileIO::setFromJSON (ChannelType, OutputChannelConfig[OM_CHANNEL_TYPE_NAME]);
            // DEBUG_V ("");

            // is it a valid / supported channel type
            if ((ChannelType < uint32_t (OutputType_Start)) || (ChannelType >= uint32_t (OutputType_End)))
            {
                // if not, flag an error and move on to the next channel
                LOG_PORT.println (String (F ("Invalid Channel Type in config '")) + ChannelType + String (F ("'. Specified for channel '")) + ChannelIndex + "'. Disabling channel");
                InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex), e_OutputType::OutputType_Disabled);
                continue;
            }
            // DEBUG_V ("");

            // do we have a configuration for the channel type?
            if (false == OutputChannelConfig.containsKey (String (ChannelType)))
            {
                // if not, flag an error and stop processing
                LOG_PORT.println (String (F ("No Output Settings Found for Channel '")) + ChannelIndex + String (F ("'. Using Defaults")));
                InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex), e_OutputType::OutputType_Disabled);
                continue;
            }

            JsonObject OutputChannelDriverConfig = OutputChannelConfig[String (ChannelType)];
            // DEBUG_V ("");

            // make sure the proper output type is running
            InstantiateNewOutputChannel (e_OutputChannelIds (ChannelIndex), e_OutputType (ChannelType));
            // DEBUG_V ("");

            // send the config to the driver. At this level we have no idea what is in it
            Response |= pOutputChannelDrivers[ChannelIndex]->SetConfig (OutputChannelDriverConfig);

        } // end for each channel

        // all went well
        Response = true;

    } while (false);

    // did we get a valid config?
    if (false == Response)
    {
        // save the current config since it is the best we have.
        // DEBUG_V ("");
        CreateNewConfig ();
    }

    UpdateDisplayBufferReferences ();

    // DEBUG_END;
    return Response;

} // ProcessJsonConfig

//-----------------------------------------------------------------------------
/*
    This is a bit tricky. The running config is only a portion of the total 
    configuration. We need to get the existing saved configuration and add the
    current configuration to it.

*   Save the current configuration to NVRAM
*
*   needs
*       Nothing
*   returns
*       Nothing
*/
void c_OutputMgr::SaveConfig ()
{
    // DEBUG_START;

    // DEBUG_V (String ("ConfigData: ") + ConfigData);

    if (FileIO::SaveConfig (ConfigFileName, ConfigData))
    {
        LOG_PORT.println (F ("**** Saved Output Manager Config File. ****"));
    } // end we got a config and it was good
    else
    {
        LOG_PORT.println (F ("EEEE Error Saving Output Manager Config File. EEEE"));
    }

    // DEBUG_END;

} // SaveConfig

//-----------------------------------------------------------------------------
/* Sets the configuration for the current active ports
*
*   Needs
*       Reference to the incoming JSON configuration doc
*   Returns
*       true - No Errors found
*       false - Had an issue and it was reported to the log interface
*/
bool c_OutputMgr::SetConfig (JsonObject & jsonConfig)
{
    // DEBUG_START;
    boolean Response = false;
    if (jsonConfig.containsKey (OM_SECTION_NAME))
    {
        // DEBUG_V ("");
        Response = ProcessJsonConfig (jsonConfig);

        // schedule a future save to the file system
        ConfigSaveNeeded = true;
    }
    else
    {
        LOG_PORT.println (F("EEEE No Output Manager settings found. EEEE"));
    }
    // DEBUG_END;
    return Response;
} // SetConfig

//-----------------------------------------------------------------------------
///< Called from loop(), renders output data
void c_OutputMgr::Render()
{
    // DEBUG_START;
    // do we need to save the current config?
    if (true == ConfigSaveNeeded)
    {
        ConfigSaveNeeded = false;
        SaveConfig ();
    } // done need to save the current config

    if (false == IsOutputPaused)
    {
        // DEBUG_START;
        for (c_OutputCommon* pOutputChannel : pOutputChannelDrivers)
        {
            pOutputChannel->Render ();
        }
    }
    // DEBUG_END;
} // render

//-----------------------------------------------------------------------------
void c_OutputMgr::UpdateDisplayBufferReferences (void)
{
    // DEBUG_START;

    uint16_t OutputBufferOffset = 0;

    // DEBUG_V (String ("        BufferSize: ") + String (sizeof(OutputBuffer)));
    // DEBUG_V (String ("OutputBufferOffset: ") + String (OutputBufferOffset));

    for (c_OutputCommon* pOutputChannel : pOutputChannelDrivers)
    {
        pOutputChannel->SetOutputBufferAddress (&OutputBuffer[OutputBufferOffset]);
        uint16_t ChannelsNeeded     = pOutputChannel->GetNumChannelsNeeded ();
        uint16_t AvailableChannels  = sizeof(OutputBuffer) - OutputBufferOffset;
        uint16_t ChannelsToAllocate = min (ChannelsNeeded, AvailableChannels);
        
        // DEBUG_V (String ("    ChannelsNeeded: ") + String (ChannelsNeeded));
        // DEBUG_V (String (" AvailableChannels: ") + String (AvailableChannels));
        // DEBUG_V (String ("ChannelsToAllocate: ") + String (ChannelsToAllocate));

        pOutputChannel->SetOutputBufferSize (ChannelsToAllocate);

        if (AvailableChannels < ChannelsNeeded)
        {
            LOG_PORT.println (String (F ("--- ERROR: Too many output channels have been defined: ")) + String (OutputBufferOffset));
        }

        OutputBufferOffset += ChannelsToAllocate;
        // DEBUG_V (String ("pOutputChannel->GetBufferUsedSize: ") + String (pOutputChannel->GetBufferUsedSize ()));
        // DEBUG_V (String ("OutputBufferOffset: ") + String(OutputBufferOffset));
    }

    // DEBUG_V (String ("   TotalBufferSize: ") + String (OutputBufferOffset));
    UsedBufferSize = OutputBufferOffset;
    InputMgr.SetBufferInfo (OutputBuffer, OutputBufferOffset);

    // DEBUG_END;

} // UpdateDisplayBufferReferences


// create a global instance of the output channel factory
c_OutputMgr OutputMgr;