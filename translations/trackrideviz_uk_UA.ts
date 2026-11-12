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
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="317"/>
        <source>The FIT file contains no timed records. It may be a settings, course or workout file, not an activity.</source>
        <translation>Файл FIT не містить записів із часом. Можливо, це файл налаштувань, маршруту чи тренування, а не запис активності.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="328"/>
        <source>%1 records without a timestamp were skipped.</source>
        <translation>Пропущено записів без позначки часу: %1.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="332"/>
        <source>%1 records with a timestamp that does not fit the rest of the recording were skipped.</source>
        <translation>Пропущено записів, час яких не узгоджується з рештою запису: %1.</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/fit/fitimporter.cpp" line="341"/>
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
        <location filename="../src/mainwindow.cpp" line="56"/>
        <source>Latitude</source>
        <translation>Широта</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="60"/>
        <source>Longitude</source>
        <translation>Довгота</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="64"/>
        <source>Elevation</source>
        <translation>Висота</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="64"/>
        <source>m</source>
        <translation>м</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="68"/>
        <source>Speed</source>
        <translation>Швидкість</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="68"/>
        <source>km/h</source>
        <translation>км/год</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="72"/>
        <source>Distance</source>
        <translation>Відстань</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="72"/>
        <source>km</source>
        <translation>км</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="76"/>
        <source>Heading</source>
        <translation>Курс</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="80"/>
        <source>Grade</source>
        <translation>Ухил</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="84"/>
        <source>Heart rate</source>
        <translation>Пульс</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="84"/>
        <source>bpm</source>
        <translation>уд/хв</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="88"/>
        <source>Cadence</source>
        <translation>Каденс</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="88"/>
        <source>rpm</source>
        <translation>об/хв</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="92"/>
        <source>Power</source>
        <translation>Потужність</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="92"/>
        <source>W</source>
        <translation>Вт</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="96"/>
        <source>Temperature</source>
        <translation>Температура</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="172"/>
        <source>Play / pause (Space)</source>
        <translation>Відтворення / пауза (Пробіл)</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="192"/>
        <source> s</source>
        <translation> с</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="194"/>
        <source>Time in the data at the first frame of the video.
Increase it if the gauges are late compared to the picture.</source>
        <translation>Час у даних, що відповідає першому кадру відео.
Збільште, якщо прилади запізнюються відносно зображення.</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="199"/>
        <source>Auto</source>
        <translation>Авто</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="200"/>
        <source>Set the offset from the recording time of the video and the data</source>
        <translation>Визначити зсув за часом запису відео та даних</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="216"/>
        <source>Data offset:</source>
        <translation>Зсув даних:</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="230"/>
        <source>Sources</source>
        <translation>Джерела</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="231"/>
        <source>Gauge library</source>
        <translation>Бібліотека приладів</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="232"/>
        <source>Properties</source>
        <translation>Властивості</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="233"/>
        <source>Timeline</source>
        <translation>Шкала часу</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="245"/>
        <source>Channel</source>
        <translation>Канал</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="245"/>
        <source>Now</source>
        <translation>Зараз</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="245"/>
        <source>Unit</source>
        <translation>Одиниця</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="245"/>
        <source>Min</source>
        <translation>Мін</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="245"/>
        <source>Max</source>
        <translation>Макс</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="251"/>
        <source>&quot;Now&quot; is the value at the video frame on screen</source>
        <translation>«Зараз» — значення для кадру відео на екрані</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="272"/>
        <source>&amp;File</source>
        <translation>&amp;Файл</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="273"/>
        <source>Open &amp;video...</source>
        <translation>Відкрити &amp;відео...</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="275"/>
        <source>Open &amp;data...</source>
        <translation>Відкрити &amp;дані...</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="278"/>
        <source>&amp;Quit</source>
        <translation>&amp;Вийти</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="281"/>
        <source>&amp;View</source>
        <translation>&amp;Вигляд</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="287"/>
        <source>&amp;Help</source>
        <translation>&amp;Довідка</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="288"/>
        <source>&amp;About</source>
        <translation>&amp;Про програму</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="289"/>
        <source>About &amp;Qt</source>
        <translation>Про &amp;Qt</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="295"/>
        <source>Open video</source>
        <translation>Відкрити відео</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="296"/>
        <source>Video files (%1);;All files (*)</source>
        <translation>Відеофайли (%1);;Усі файли (*)</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="309"/>
        <location filename="../src/mainwindow.cpp" line="360"/>
        <source>Open data</source>
        <translation>Відкрити дані</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="333"/>
        <source>Loading %1...</source>
        <translation>Завантаження %1...</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="415"/>
        <location filename="../src/mainwindow.cpp" line="464"/>
        <source>Video: %1</source>
        <translation>Відео: %1</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="423"/>
        <source>Set by hand.</source>
        <translation>Задано вручну.</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="442"/>
        <source>Not synchronized: %1 Set the offset by hand.</source>
        <translation>Не синхронізовано: %1 Задайте зсув вручну.</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="464"/>
        <location filename="../src/mainwindow.cpp" line="468"/>
        <source>none</source>
        <translation>немає</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="468"/>
        <location filename="../src/mainwindow.cpp" line="472"/>
        <source>Data: %1</source>
        <translation>Дані: %1</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="476"/>
        <source>Start: %1 UTC</source>
        <translation>Початок: %1 UTC</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="478"/>
        <source>Duration: %1</source>
        <translation>Тривалість: %1</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="479"/>
        <source>Laps: %1, pauses: %2</source>
        <translation>Кіл: %1, пауз: %2</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="483"/>
        <source>Warnings:</source>
        <translation>Попередження:</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/mainwindow.cpp" line="509"/>
        <source>%1: %n samples</source>
        <translation>
            <numerusform>%1: %n відлік</numerusform>
            <numerusform>%1: %n відліки</numerusform>
            <numerusform>%1: %n відліків</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="540"/>
        <source>About</source>
        <translation>Про програму</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="541"/>
        <source>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;Version %2&lt;br&gt;&lt;br&gt;Telemetry overlays for action camera video.&lt;br&gt;Licensed under the GNU GPL v3.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;Версія %2&lt;br&gt;&lt;br&gt;Накладання телеметрії на відео з екшн-камер.&lt;br&gt;Ліцензія GNU GPL v3.</translation>
    </message>
</context>
<context>
    <name>TelemetryImporter</name>
    <message>
        <location filename="../src/core/telemetry/telemetryimporter.cpp" line="25"/>
        <source>Telemetry files (%1);;All files (*)</source>
        <translation>Файли телеметрії (%1);;Усі файли (*)</translation>
    </message>
    <message>
        <location filename="../src/core/telemetry/telemetryimporter.cpp" line="41"/>
        <source>%1: unsupported file type. Supported: %2.</source>
        <translation>%1: непідтримуваний тип файлу. Підтримуються: %2.</translation>
    </message>
</context>
<context>
    <name>VideoCanvas</name>
    <message>
        <location filename="../src/videocanvas.cpp" line="478"/>
        <source>Open a video to start</source>
        <translation>Відкрийте відео, щоб почати</translation>
    </message>
    <message>
        <location filename="../src/videocanvas.cpp" line="479"/>
        <source>The video cannot be shown: OpenGL is not available.</source>
        <translation>Неможливо показати відео: OpenGL недоступний.</translation>
    </message>
</context>
<context>
    <name>VideoSync</name>
    <message>
        <location filename="../src/core/sync/videosync.cpp" line="48"/>
        <source>The video file name holds no recording time.</source>
        <translation>У назві відеофайлу немає часу запису.</translation>
    </message>
    <message>
        <location filename="../src/core/sync/videosync.cpp" line="53"/>
        <source>The data has no absolute time.</source>
        <translation>Дані не містять абсолютного часу.</translation>
    </message>
    <message>
        <location filename="../src/core/sync/videosync.cpp" line="66"/>
        <source>time zone from the data, %1</source>
        <translation>часовий пояс із даних, %1</translation>
    </message>
    <message>
        <location filename="../src/core/sync/videosync.cpp" line="67"/>
        <source>time zone of this computer, %1</source>
        <translation>часовий пояс цього комп’ютера, %1</translation>
    </message>
    <message>
        <location filename="../src/core/sync/videosync.cpp" line="72"/>
        <source>File name time (%1).</source>
        <translation>Час із назви файлу (%1).</translation>
    </message>
    <message>
        <location filename="../src/core/sync/videosync.cpp" line="88"/>
        <source>File name time (%1) did not overlap the data; shifted by %2 min to make it fit. Check it.</source>
        <translation>Час із назви файлу (%1) не збігався з даними; зсунуто на %2 хв, щоб сумістити. Перевірте.</translation>
    </message>
    <message>
        <location filename="../src/core/sync/videosync.cpp" line="95"/>
        <source>The video and the data were not recorded at the same time.</source>
        <translation>Відео та дані записано в різний час.</translation>
    </message>
</context>
</TS>
