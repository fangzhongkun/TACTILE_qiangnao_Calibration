#include "config.h"
#include "logging.h"
#include "mainwindow.h"
#include "scanner.h"
#include "version.h"

#include <fmt/format.h>
#include <probe/cpu.h>
#include <probe/system.h>
#include <probe/thread.h>
#include <probe/util.h>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

#ifdef _WIN32
    ::SetConsoleOutputCP(CP_UTF8);
    ::setvbuf(stdout, nullptr, _IONBF, 0);
#endif

    probe::thread::set_name("SCANNER-MAIN");

    Logger::init(argv[0]);

    logi("ScannerPT");
    logi(" -- Version          : {}", SCANNER_VERSION);
    logi(" -- Qt               : {}", qVersion());
    logi(" -- Operating System : {} ({})", probe::system::name(),
         probe::to_string(probe::system::version()));
    logi(" -- Kernel           : {} ", probe::system::kernel::name(),
         probe::to_string(probe::system::kernel::version()));
    logi(" -- CPU              : {}", probe::cpu::info().name);
    logi(" -- Architecture     : {} ({})", probe::to_string(probe::cpu::architecture()),
         probe::to_string(probe::cpu::endianness()));

    // settings
    config::load();

    // application
    Scanner app(argc, argv);

    QTranslator translator{};
    const auto  file = "languages/scanner_" + config::language;
    loge_if(!translator.load(file), "failed to load '{}'", file.toStdString());
    Scanner::installTranslator(&translator);

    // main window
    auto window = new MainWindow();
    window->show();

    return Scanner::exec();
}
