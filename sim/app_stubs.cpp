#include <apps/common/weather/weather.h>
#include <apps/app_setup/workers/workers.h>

namespace weather {

void tick() {}

Snapshot snapshot()
{
    Snapshot s;
    s.valid       = true;
    s.hasTemp     = true;
    s.tempC       = 22;
    s.temperature = "22C";
    s.tempDisplay = "22\xC2\xB0" "C";
    s.condition   = "Clear";
    s.windDir     = "SW";
    s.windLevel   = "Lv.2";
    s.windText    = "SW Lv.2";
    s.humidity    = "45%";
    s.aqiText     = "AQ18";
    s.aqiValue    = 18;
    s.aqiLevel    = AqiLevel::Good;
    return s;
}

}  // namespace weather

namespace setup_workers {

struct CheckUpdateWorker::CheckTaskContext {};
struct CheckUpdateWorker::OtaTaskContext {};
class CheckUpdateWorker::CheckUpdateView {};

CheckUpdateWorker::CheckUpdateWorker() { _is_done = true; }
CheckUpdateWorker::~CheckUpdateWorker() = default;
void CheckUpdateWorker::update() { _is_done = true; }
void CheckUpdateWorker::_startCheckTask() {}
void CheckUpdateWorker::_startOtaTask() {}
void CheckUpdateWorker::_stopTask() {}

}  // namespace setup_workers
