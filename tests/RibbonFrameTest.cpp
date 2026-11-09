#include "RibbonFrame.h"
#include <iostream>
int main()
{
    const QString valid = R"({"version":1,"active":true,"rgb":[[255,0,0],[0,255,0],[0,0,255],[90,80,70]]})";
    const auto colors = decodeRibbonFrame(valid);
    if (!colors || (*colors)[0] != QColor(Qt::red) || (*colors)[2] != QColor(Qt::blue)) return 1;
    auto paused = valid; paused.replace("true", "false");
    const auto black = decodeRibbonFrame(paused);
    if (!black || (*black)[0] != QColor(Qt::black)) return 2;
    for (const auto &bad : {QString("{}"), QString("not json"), QString("[]"),
                           QString(valid).replace("255", "256"),
                           QString(valid).replace("255", "0.5"),
                           QString(valid).replace("true", "1"),
                           QString(valid).replace("\"version\":1", "\"version\":2")})
        if (decodeRibbonFrame(bad)) return 3;
    std::cout << "PASS: ribbon colors, paused state, malformed and out-of-range frames\n";
}
