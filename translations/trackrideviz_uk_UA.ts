<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="uk_UA">
<context>
    <name>FitDecoder</name>
    <message>
        <location filename="../src/core/telemetry/fit/fitdecoder.cpp" line="252"/>
        <source>Data for local message type %1 appears before its definition; the rest of the file was skipped.</source>
        <translation>Дані для локального типу повідомлення %1 зустрілися раніше за його визначення; решту файлу пропущено.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitdecoder.cpp" line="350"/>
        <location filename="../src/core/telemetry/fit/fitdecoder.cpp" line="415"/>
        <source>This is not a FIT file.</source>
        <translation>Це не файл FIT.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitdecoder.cpp" line="353"/>
        <source>Unrecognized data after the end of the FIT file was ignored.</source>
        <translation>Нерозпізнані дані після кінця файлу FIT проігноровано.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitdecoder.cpp" line="363"/>
        <source>The file header checksum is wrong.</source>
        <translation>Неправильна контрольна сума заголовка файлу.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitdecoder.cpp" line="408"/>
        <source>The file checksum is wrong; the data may be damaged.</source>
        <translation>Неправильна контрольна сума файлу; дані можуть бути пошкоджені.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitdecoder.cpp" line="420"/>
        <source>The file is incomplete; everything recorded before the cut was read.</source>
        <translation>Файл неповний; прочитано все, що було записано до обриву.</translation>
    </message>
</context>
<context>
    <name>FitImporter</name>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="133"/>
        <source>Cannot open %1: %2</source>
        <translation>Не вдалося відкрити %1: %2</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="281"/>
        <source>The FIT file contains no timed records. It may be a settings, course or workout file, not an activity.</source>
        <translation>Файл FIT не містить записів із часом. Можливо, це файл налаштувань, маршруту чи тренування, а не запис активності.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="292"/>
        <source>%1 records without a timestamp were skipped.</source>
        <translation>Пропущено записів без позначки часу: %1.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="296"/>
        <source>%1 records with a timestamp that does not fit the rest of the recording were skipped.</source>
        <translation>Пропущено записів, час яких не узгоджується з рештою запису: %1.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="305"/>
        <source>The file has no absolute time; it has to be synchronized with the video manually.</source>
        <translation>У файлі немає абсолютного часу; його потрібно синхронізувати з відео вручну.</translation>
    </message>
</context>
<context>
    <name>GpxImporter</name>
    <message>
        <location filename="../src/core/telemetry/gpximporter.cpp" line="152"/>
        <source>Cannot open %1: %2</source>
        <translation>Не вдалося відкрити %1: %2</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/gpximporter.cpp" line="201"/>
        <source>Not a valid GPX file: %1 (line %2)</source>
        <translation>Некоректний файл GPX: %1 (рядок %2)</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/gpximporter.cpp" line="211"/>
        <source>The GPX track has no timestamps, so it cannot be synchronized with video.</source>
        <translation>Трек GPX не містить позначок часу, тому його неможливо синхронізувати з відео.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/gpximporter.cpp" line="212"/>
        <source>The GPX file contains no track points.</source>
        <translation>Файл GPX не містить точок треку.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/gpximporter.cpp" line="221"/>
        <source>The file is damaged after line %1; only the points before it were read.</source>
        <translation>Файл пошкоджено після рядка %1; прочитано лише точки до нього.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/gpximporter.cpp" line="226"/>
        <source>%1 points without a timestamp were skipped.</source>
        <translation>Пропущено точок без позначки часу: %1.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/gpximporter.cpp" line="294"/>
        <source>%1 points with repeated or backward time were skipped.</source>
        <translation>Пропущено точок із повторюваним або зворотним часом: %1.</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../src/mainwindow.cpp" line="17"/>
        <source>Open a video to start</source>
        <translation>Відкрийте відео, щоб почати</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="39"/>
        <source>Sources</source>
        <translation>Джерела</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="40"/>
        <source>Gauge library</source>
        <translation>Бібліотека приладів</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="41"/>
        <source>Properties</source>
        <translation>Властивості</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="42"/>
        <source>Timeline</source>
        <translation>Шкала часу</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="59"/>
        <source>&amp;File</source>
        <translation>&amp;Файл</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="60"/>
        <source>&amp;Quit</source>
        <translation>&amp;Вийти</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="63"/>
        <source>&amp;View</source>
        <translation>&amp;Вигляд</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="69"/>
        <source>&amp;Help</source>
        <translation>&amp;Довідка</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="70"/>
        <source>&amp;About</source>
        <translation>&amp;Про програму</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="71"/>
        <source>About &amp;Qt</source>
        <translation>Про &amp;Qt</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="76"/>
        <source>About</source>
        <translation>Про програму</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="77"/>
        <source>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;Version %2&lt;br&gt;&lt;br&gt;Telemetry overlays for action camera video.&lt;br&gt;Licensed under the GNU GPL v3.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;Версія %2&lt;br&gt;&lt;br&gt;Накладання телеметрії на відео з екшн-камер.&lt;br&gt;Ліцензія GNU GPL v3.</translation>
    </message>
</context>
</TS>
