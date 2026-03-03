#include "Demo/mb_ddf_demo_01_dds_init_pubsub.h"
#include "Demo/mb_ddf_demo_02_sub_read_modes.h"
#include "Demo/mb_ddf_demo_03_hardware_factory.h"
#include "Demo/mb_ddf_demo_04_pubandsub_factory_dds.h"
#include "Demo/mb_ddf_demo_05_system_timer.h"
#include "Demo/mb_ddf_demo_06_chrono_helper.h"
#include "Demo/mb_ddf_demo_07_logger.h"
#include "Demo/mb_ddf_demo_08_spi_flash.h"
#include "Demo/mb_ddf_demo_09_canfd_device.h"

#include "MB_DDF/Debug/Logger.h"
#include "MB_DDF/Debug/LoggerExtensions.h"

#include <exception>

int main() {
    LOG_SET_LEVEL_INFO();
    LOG_DISABLE_TIMESTAMP();
    LOG_ENABLE_COLOR();
    LOG_DISABLE_FUNCTION_LINE();

    try {
        LOG_TITLE("CAN FD Device Test");
        Demo::MB_DDF_Demos::RunDemo09_CanFdDeviceTest();

        // LOG_TITLE("DDS Init and PubSub");
        // Demo::MB_DDF_Demos::RunDemo01_DDSInitAndPubSub();

        // LOG_TITLE("Subscribe Callback and Polling Read");
        // Demo::MB_DDF_Demos::RunDemo02_SubscribeCallbackAndPollingRead();

        // LOG_TITLE("All Hardware Factory Examples");
        // Demo::MB_DDF_Demos::RunDemo03_AllHardwareFactoryExamples();

        // LOG_TITLE("PubAndSub Quick Create with Hardware Factory");
        // Demo::MB_DDF_Demos::RunDemo04_PubAndSubQuickCreateWithHardwareFactory();

        // LOG_TITLE("System Timer Usage");
        // Demo::MB_DDF_Demos::RunDemo05_SystemTimerUsage();

        // LOG_TITLE("Chrono Helper Usage");
        // Demo::MB_DDF_Demos::RunDemo06_ChronoHelperUsage();

        // LOG_TITLE("SPI Flash Test DYT");
        // Demo::MB_DDF_Demos::RunDemo08_SPIFlashTest(false);

        // LOG_TITLE("SPI Flash Test SJL");
        // Demo::MB_DDF_Demos::RunDemo08_SPIFlashTest(true);

        // LOG_TITLE("Logger Usage");
        // Demo::MB_DDF_Demos::RunDemo07_LoggerUsage();

    } catch (const std::exception& e) {
        LOG_ERROR << "unhandled exception: " << e.what();
        return 1;
    } catch (...) {
        LOG_ERROR << "unhandled unknown exception";
        return 2;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    LOG_INFO << "all demos finished";
    return 0;
}
