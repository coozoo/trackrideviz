#include "demosession.h"

#include "channelids.h"

#include <cmath>

TelemetrySessionPtr DemoSession::create()
{
    const double pi = 3.14159265358979323846;
    const int seconds = 600;
    const double lapLength = seconds / 2.0;
    auto session = std::make_shared<TelemetrySession>();
    Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
    Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
    Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
    Channel &distance = session->channelForWriting(ChannelId::Distance, "m");
    Channel &elevation = session->channelForWriting(ChannelId::Elevation, "m");
    Channel &heading = session->channelForWriting(ChannelId::Heading, "deg");
    Channel &grade = session->channelForWriting(ChannelId::Grade, "%");
    Channel &heartRate = session->channelForWriting(ChannelId::HeartRate, "bpm");
    Channel &cadence = session->channelForWriting(ChannelId::Cadence, "rpm");
    Channel &power = session->channelForWriting(ChannelId::Power, "W");
    Channel &temperature = session->channelForWriting(ChannelId::Temperature, "C");
    Channel &lateral = session->channelForWriting(ChannelId::GForceLateral, "g");
    Channel &longitudinal = session->channelForWriting(ChannelId::GForceLongitudinal, "g");
    Channel &vertical = session->channelForWriting(ChannelId::GForceVertical, "g");
    // a power meter in each pedal
    Channel &balance = session->channelForWriting(ChannelId::PowerBalanceLeft, "%");
    Channel &balanceRight = session->channelForWriting(ChannelId::PowerBalanceRight, "%");
    Channel &leftPower = session->channelForWriting(ChannelId::LeftPower, "W");
    Channel &rightPower = session->channelForWriting(ChannelId::RightPower, "W");
    Channel &leftOffset = session->channelForWriting(ChannelId::LeftPlatformCenterOffset, "mm");
    Channel &rightOffset = session->channelForWriting(ChannelId::RightPlatformCenterOffset, "mm");
    Channel &leftStart = session->channelForWriting(ChannelId::LeftPowerPhaseStart, "deg");
    Channel &leftLength = session->channelForWriting(ChannelId::LeftPowerPhaseLength, "deg");
    Channel &leftPeakStart = session->channelForWriting(ChannelId::LeftPowerPhasePeakStart, "deg");
    Channel &leftPeakLength = session->channelForWriting(ChannelId::LeftPowerPhasePeakLength, "deg");
    Channel &rightStart = session->channelForWriting(ChannelId::RightPowerPhaseStart, "deg");
    Channel &rightLength = session->channelForWriting(ChannelId::RightPowerPhaseLength, "deg");
    Channel &rightPeakStart = session->channelForWriting(ChannelId::RightPowerPhasePeakStart, "deg");
    Channel &rightPeakLength = session->channelForWriting(ChannelId::RightPowerPhasePeakLength, "deg");

    double travelled = 0.0;
    for (int t = 0; t <= seconds; ++t)
        {
            // a lap around an oval with a kink in it, twice
            const double a = 2.0 * pi * std::fmod(t, lapLength) / lapLength;
            lat.append(t, 50.0 + 0.004 * std::sin(a) + 0.0008 * std::sin(3.0 * a));
            lon.append(t, 10.0 + 0.008 * std::cos(a));
            const double v = (28.0 + 10.0 * std::sin(2.0 * a) + 3.0 * std::sin(0.11 * t)) / 3.6;
            speed.append(t, v);
            travelled += v;
            distance.append(t, travelled);
            elevation.append(t, 230.0 + 25.0 * std::sin(a + 0.6));
            heading.append(t, std::fmod(360.0 + 90.0 - a * 180.0 / pi, 360.0));
            grade.append(t, 6.0 * std::cos(a + 0.6));
            heartRate.append(t, 140.0 + 22.0 * std::sin(a + 1.0) + 0.02 * t);
            cadence.append(t, 85.0 + 8.0 * std::sin(2.0 * a));
            const double watts = 220.0 + 90.0 * std::cos(a + 0.6);
            power.append(t, watts);
            const double left = 48.0 + 3.0 * std::sin(a + 0.6);
            balance.append(t, left);
            balanceRight.append(t, 100.0 - left);
            leftPower.append(t, watts * left / 100.0);
            rightPower.append(t, watts * (100.0 - left) / 100.0);
            leftOffset.append(t, -2.0 + 3.0 * std::sin(1.3 * a));
            rightOffset.append(t, 3.0 + 2.0 * std::cos(1.7 * a));
            // harder pedalling starts the push earlier and makes it longer
            const double push = (watts - 220.0) / 90.0;
            leftStart.append(t, 15.0 - 8.0 * push);
            leftLength.append(t, 195.0 + 15.0 * push);
            leftPeakStart.append(t, 72.0 - 6.0 * push);
            leftPeakLength.append(t, 58.0 + 10.0 * push);
            rightStart.append(t, 20.0 - 8.0 * push);
            rightLength.append(t, 188.0 + 15.0 * push);
            rightPeakStart.append(t, 76.0 - 6.0 * push);
            rightPeakLength.append(t, 54.0 + 10.0 * push);
            temperature.append(t, 23.0 + 0.002 * t);
            lateral.append(t, 0.6 * std::sin(2.0 * a));
            longitudinal.append(t, 0.4 * std::cos(2.0 * a));
            // 1 g riding level, more over the bumps and in the dips of the road
            vertical.append(t, 1.0 + 0.3 * std::sin(7.0 * a) + 0.15 * std::sin(0.9 * t));
        }
    session->addLap({0.0, lapLength});
    session->addLap({lapLength, double(seconds)});
    session->setSourceInfo("Demo");
    return session;
}
