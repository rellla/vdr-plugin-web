/*
 * web.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <getopt.h>
#include <vdr/plugin.h>
#include <vdr/remote.h>
#include <Magick++.h>
#include "web.h"
#include "browserclient.h"
#include "ini.h"
#include "httplib.h"
#include "webosdpage.h"
#include "status.h"
#include "videocontrol.h"
#include "sharedmemory.h"
#include "dummyosd.h"

std::string browserIp;
int browserPort;

std::string transcoderIp;
int transcoderPort;

std::string vdrIp;
int vdrPort;

httplib::Server vdrServer;
cHbbtvDeviceStatus *hbbtvDeviceStatus;

VideoPlayer* videoPlayer = nullptr;

enum OSD_COMMAND {
    OPEN,
    REOPEN,
    CLOSE
};

OSD_COMMAND nextOsdCommand = OPEN;
bool useOutputDeviceScale = false;
bool useDummyOsd = false;

int nr = 0;

int lastVideoX, lastVideoY, lastVideoWidth, lastVideoHeight;

void startHttpServer(std::string vdrIp, int vdrPort) {

    vdrServer.Post("/ProcessOsdUpdate", [](const httplib::Request &req, httplib::Response &res) {
        auto render_width = req.get_param_value("disp_width");
        auto render_height = req.get_param_value("disp_height");
        auto x = req.get_param_value("x");
        auto y = req.get_param_value("y");
        auto width = req.get_param_value("width");
        auto height = req.get_param_value("height");

        // dsyslog("[vdrweb] Incoming request /ProcessOsdUpdate with width %s, height %s", width.c_str(), height.c_str());

        if (width.empty() || height.empty()) {
            res.status = 404;
        } else {
            WebOSDPage* page = WebOSDPage::Get();
            if (page == nullptr) {
                // illegal request -> abort
                esyslog("[vdrweb] ProcessOsdUpdate: osd update request while webOsdPage is null.");
                res.status = 404;
                return;
            }

            bool result = page->drawImage(sharedMemory.Get(), std::stoi(render_width), std::stoi(render_height), std::stoi(x), std::stoi(y), std::stoi(width), std::stoi(height));

            if (result) {
                res.status = 200;
                res.set_content("ok", "text/plain");
            }  else {
                res.status = 500;
                res.set_content("error", "text/plain");
            }
        }
    });

    vdrServer.Post("/ProcessOsdUpdateQOI", [](const httplib::Request &req, httplib::Response &res) {
        const std::string body = req.body;

        WebOSDPage* page = WebOSDPage::Get();
        if (page == nullptr) {
            // illegal request -> abort
            esyslog("[vdrweb] ProcessOsdUpdateQOI: osd update request while webOsdPage is null.");
            res.status = 404;
            return;
        }

        bool result = page->drawImageQOI(body);

        if (result) {
            res.status = 200;
            res.set_content("ok", "text/plain");
        }  else {
            res.status = 500;
            res.set_content("error", "text/plain");
        }
    });

    vdrServer.Post("/ProcessTSPacket", [](const httplib::Request &req, httplib::Response &res) {
        const std::string body = req.body;

        if (body.empty()) {
            res.status = 404;
        } else {
            /*
            FILE* f = fopen("test.ts", "a");
            fwrite((uint8_t *)body.c_str(), body.length(), 1, f);
            fclose(f);
            */

            if (videoPlayer != nullptr) {
                videoPlayer->PlayPacket((uint8_t *) body.c_str(), (int) body.length());
            }

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    vdrServer.Get("/StartVideo", [](const httplib::Request &req, httplib::Response &res) {
        dsyslog("[vdrweb] StartVideo received");

        // Close existing OSD
        nextOsdCommand = CLOSE;
        cRemote::CallPlugin("web");

        WebOSDPage* page = WebOSDPage::Create(useOutputDeviceScale, PLAYER);
        page->Display();

        videoPlayer = new VideoPlayer();
        page->SetPlayer(videoPlayer);

        cControl::Launch(page);
        cControl::Attach();

        res.status = 200;
        res.set_content("ok", "text/plain");
    });

    vdrServer.Get("/StopVideo", [](const httplib::Request &req, httplib::Response &res) {
        dsyslog("[vdrweb] StopVideo received");

        if (videoPlayer != nullptr) {
            delete videoPlayer;
            videoPlayer = nullptr;

            nextOsdCommand = REOPEN;
            cRemote::CallPlugin("web");
        }

        res.status = 200;
        res.set_content("ok", "text/plain");
    });

    vdrServer.Get("/PauseVideo", [](const httplib::Request &req, httplib::Response &res) {
        dsyslog("[vdrweb] PauseVideo received");

        if (videoPlayer != nullptr) {
            videoPlayer->Pause();
        }

        res.status = 200;
        res.set_content("ok", "text/plain");
    });

    vdrServer.Get("/ResumeVideo", [](const httplib::Request &req, httplib::Response &res) {
        dsyslog("[vdrweb] ResumeVideo received");

        if (videoPlayer != nullptr) {
            videoPlayer->Resume();
        }

        res.status = 200;
        res.set_content("ok", "text/plain");
    });

    vdrServer.Post("/VideoSize", [](const httplib::Request &req, httplib::Response &res) {
        auto x = req.get_param_value("x");
        auto y = req.get_param_value("y");
        auto w = req.get_param_value("w");
        auto h = req.get_param_value("h");

        dsyslog("[vdrweb] Incoming request /VideoSize with x %s, y %s, width %s, height %s", x.c_str(), y.c_str(), w.c_str(), h.c_str());

        if (x.empty() || y.empty() || w.empty() || h.empty()) {
            res.status = 404;
        } else {
            lastVideoX = std::atoi(x.c_str());
            lastVideoY = std::atoi(y.c_str());
            lastVideoWidth = std::atoi(w.c_str());
            lastVideoHeight = std::atoi(h.c_str());

            videoPlayer->SetVideoSize(lastVideoX, lastVideoY, lastVideoWidth, lastVideoHeight);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    vdrServer.Get("/VideoFullscreen", [](const httplib::Request &req, httplib::Response &res) {
        dsyslog("[vdrweb] VideoFullscreen received");

        lastVideoX = lastVideoY = lastVideoWidth = lastVideoHeight = 0;

        videoPlayer->setVideoFullscreen();

        res.status = 200;
        res.set_content("ok", "text/plain");
    });

    vdrServer.Get("/Hello", [](const httplib::Request &req, httplib::Response &res) {
        dsyslog("[vdrweb] Hello received");

        browserClient->HelloFromBrowser();

        res.status = 200;
        res.set_content("ok", "text/plain");
    });

    vdrServer.Get("/ResetVideo", [](const httplib::Request &req, httplib::Response &res) {
        dsyslog("[vdrweb] ResetVideo received: Coords x=%d, y=%d, w=%d, h=%d", lastVideoX, lastVideoY, lastVideoWidth, lastVideoHeight);

        if (videoPlayer != nullptr) {
            videoPlayer->ResetVideo();
            videoPlayer->SetVideoSize(lastVideoX, lastVideoY, lastVideoWidth, lastVideoHeight);
        }

        res.status = 200;
        res.set_content("ok", "text/plain");
    });

    vdrServer.listen(vdrIp, vdrPort);
}

cPluginWeb::cPluginWeb() {
    // Initialize any member variables here.
    // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
    // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
}

cPluginWeb::~cPluginWeb() {
}

const char *cPluginWeb::CommandLineHelp() {
    // Return a string that describes all known command line options.
    return nullptr;
}

bool cPluginWeb::ProcessArgs(int argc, char *argv[]) {
    static struct option long_options[] = {
            { "config",      required_argument, nullptr, 'c' },
            { "fastscale",   optional_argument, nullptr, 'f' },
            { "dummyosd",    optional_argument, nullptr, 'o' },
            { nullptr }
    };

    int c, option_index = 0;
    while ((c = getopt_long(argc, argv, "c:fo", long_options, &option_index)) != -1)
    {
        switch (c)
        {
            case 'c':
                if (!readConfiguration(optarg)) {
                    exit(-1);
                }
                break;

            case 'f':
                useOutputDeviceScale = true;
                break;

            case 'o':
                useDummyOsd = true;
                break;

            default:
                break;
        }
    }
    return true;
}

bool cPluginWeb::Initialize() {
    // Initialize any background activities the plugin shall perform.
    Magick::InitializeMagick(nullptr);
    return true;
}

bool cPluginWeb::Start() {
    isyslog("[vdrweb] Start hbbtv url collector");
    hbbtvDeviceStatus = new cHbbtvDeviceStatus();

    isyslog("[vdrweb] Start Http Server on %s:%d", vdrIp.c_str(), vdrPort);
    std::thread t1(startHttpServer, vdrIp, vdrPort);
    t1.detach();

    new BrowserClient(browserIp, browserPort);

    return true;
}

void cPluginWeb::Stop() {
    vdrServer.stop();
    delete browserClient;
}

void cPluginWeb::Housekeeping() {
    // Perform any cleanup or other regular tasks.
}

void cPluginWeb::MainThreadHook() {
    // Perform actions in the context of the main program thread.
    // WARNING: Use with great care - see PLUGINS.html!

    if (WebOSDPage::Get() == nullptr && videoPlayer != nullptr) {
        delete videoPlayer;
        videoPlayer = nullptr;
    }
}

cString cPluginWeb::Active() {
    // Return a message string if shutdown should be postponed
    return nullptr;
}

time_t cPluginWeb::WakeupTime() {
    // Return custom wakeup time for shutdown script
    return 0;
}

cOsdObject *cPluginWeb::MainMenuAction() {
    dsyslog("[vdrweb] MainMenuAction: command = %d\n", (int)nextOsdCommand);

    if (nextOsdCommand == OPEN || nextOsdCommand == REOPEN) {
        WebOSDPage* page = WebOSDPage::Create(useOutputDeviceScale, OSD);

        if (nextOsdCommand == OPEN) {
            LOCK_CHANNELS_READ
            const cChannel *currentChannel = Channels->GetByNumber(cDevice::CurrentChannel());
            browserClient->RedButton(*currentChannel->GetChannelID().ToString());
        }

        nextOsdCommand = OPEN;
        return page;
    }

    nextOsdCommand = OPEN;

    if (useDummyOsd) {
        // instead of really closing the OSD, open a dummy OSD
        return new cDummyOsdObject();
    } else {
        // close OSD
        return nullptr;
    }

}

cMenuSetupPage *cPluginWeb::SetupMenu() {
    // Return a setup menu in case the plugin supports one.
    return nullptr;
}

bool cPluginWeb::SetupParse(const char *Name, const char *Value) {
    // Parse your own setup parameters and store their values.
    return false;
}

bool cPluginWeb::Service(const char *Id, void *Data) {
    // Handle custom service requests from other plugins
    return false;
}

const char **cPluginWeb::SVDRPHelpPages() {
    // Return help text for SVDRP commands this plugin implements
    return nullptr;
}

cString cPluginWeb::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode) {
    // Process SVDRP commands this plugin implements
    return nullptr;
}

bool cPluginWeb::readConfiguration(const char* configFile) {
    mINI::INIFile file(configFile);
    mINI::INIStructure ini;
    auto result = file.read(ini);

    if (!result) {
        esyslog("[vdrweb] Unable to read config file: %s", configFile);
        return false;
    }

    try {
        browserIp = ini["browser"]["http_ip"];
        if (browserIp.empty()) {
            esyslog("[vdrweb] http ip (browser) not found");
            return false;
        }

        std::string tmpBrowserPort = ini["browser"]["http_port"];
        if (tmpBrowserPort.empty()) {
            esyslog("[vdrweb] http port (browser) not found");
            return false;
        }

        transcoderIp = ini["transcoder"]["http_ip"];
        if (transcoderIp.empty()) {
            esyslog("[vdrweb] http ip (transcoder) not found");
            return false;
        }

        std::string tmpTranscoderPort = ini["transcoder"]["http_port"];
        if (tmpTranscoderPort.empty()) {
            esyslog("[vdrweb] http port (transcoder) not found");
            return false;
        }

        vdrIp = ini["vdr"]["http_ip"];
        if (vdrIp.empty()) {
            esyslog("[vdrweb] http ip (vdr) not found");
            return false;
        }

        std::string tmpVdrPort = ini["vdr"]["http_port"];
        if (tmpVdrPort.empty()) {
            esyslog("[vdrweb] http port (vdr) not found");
            return false;
        }

        browserPort = std::stoi(tmpBrowserPort);
        transcoderPort = std::stoi(tmpTranscoderPort);
        vdrPort = std::stoi(tmpVdrPort);
    } catch (...) {
        esyslog("[vdrweb] configuration error. aborting...");
        return false;
    }

    return true;
}

VDRPLUGINCREATOR(cPluginWeb); // Don't touch this!
