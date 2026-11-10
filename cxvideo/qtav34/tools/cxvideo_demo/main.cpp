#include "compat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "renderer_software.h"
#include "cxframe.h"
#include <qapplication.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[])
{
    bool showVersion = false;
    const char* mediaFile = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
            showVersion = true;
        else if (argv[i][0] != '-')
            mediaFile = argv[i]; // M3-B:实播入口(文件参数)
    }

    QApplication app(argc, argv);
    cxSetLocaleNumericC(); // Qt 抢 locale 后回拨(mpv 硬性需求)

    if (showVersion) {
        printf("cxvideo_demo (qtav34) %s\n", CXVIDEO_VERSION);
        return 0;
    }

    CXVideoPlayer player;
    player.setEngine(CxEngineAuto); // M0: 工厂恒返回 0,error 信号会发出

    if (mediaFile != 0) {
        // M3-B:实播 demo(开盘→播放→finished 退出)
        QObject::connect(&player, SIGNAL(finished()), &app, SLOT(quit()));
        if (!player.open(qFromUtf8(mediaFile))) {
            fprintf(stderr, "demo: open FAIL: %s\n", mediaFile);
            return 1;
        }
        player.play();
    }

    CXRendererSoftware view(0);
    view.resize(640, 360);
    view.show();

    player.setRenderer(&view);

    // 局部自证:软渲通路能接收一帧(letterbox 黑边示意;实播时保留原始帧)
    if (mediaFile == 0) {
        CXVideoFrame f;
        f.ptsMs = 0;
        f.width = 64;
        f.height = 64;
        f.isKey = true;
        f.img = cxNewImage(64, 64);
        f.img.fill(0xff3366aa);
        view.showFrame(&f);
    }

    return app.exec();
}
