#include "projectdata.h"

#include "../telemetry/derivedchannels.h"

TelemetrySessionPtr ProjectData::workedOut(const TelemetrySessionPtr &raw, const QVector<DataEdit> &edits,
                                           Terrain::Use elevation, const PowerEstimate::Rider &rider,
                                           const QVector<SurfaceStretch> &surfaces, const WeatherChoice &weatherChoice,
                                           const HandWind &handWind, const Drivetrain &drivetrain, bool *waiting,
                                           bool *weatherWaiting)
{
    // the gears want the speed and the cadence only
    const TelemetrySessionPtr complete = Gears::apply(DerivedChannels::complete(Terrain::apply(DataEdits::apply(raw, edits),
                                         elevation, waiting)), drivetrain);
    *weatherWaiting = false;
    // a wind entered by hand stands in for the weather history's
    const Weather weather = complete && weatherChoice.enabled && !handWind.used
                            ? WeatherStore::instance()->along(*complete, weatherChoice, weatherWaiting) : Weather();
    const TelemetrySessionPtr windy = handWind.used ? WeatherChannels::addWind(complete, handWind, weatherChoice.windShare)
                                      : WeatherChannels::add(complete, weather, weatherChoice.windShare);
    // the estimate wants the speed, the grade and the wind; what is worked out from a power wants the estimate
    const TelemetrySessionPtr powered = PowerEstimate::apply(windy, rider, surfaces);
    return powered == complete ? complete : DerivedChannels::complete(powered);
}
