#include <iostream>
#include <string.h>
#include <cinttypes>
#include "lib/nettools.hpp"
#include "lib/socket.hpp"
#include "lib/hart_mux.hpp"
#include "lib/data_types.hpp"
#include "lib/udp_gateway_discovery.hpp"
#include "lib/cpp-httplib/httplib.h"

using namespace std;
using httplib::Server;
using httplib::Request;
using httplib::Response;
using httplib::DataSink;

// NOTE: this must be compiled with the -pthread flag because the httpserver is multithreaded

string BCAST_ADDR = "192.168.1.255";

json find_gw_by_sn(string bcast_addr, string serialNo) {
    json gws = discoverGWs(bcast_addr);
    json gwData = NULL;
    for (int i=0; i<gws.size(); i++) {
        if (gws[i]["serialNo"] == serialNo) {
            gwData = gws[i];
            break;
        }
    }
    return gwData;
}

json with_gw_data_or_error(Request req, Response &res, json (*gwDataHandler)(Request, json)) {
    string serialNo(req.matches[1]);
    json gwData = find_gw_by_sn(BCAST_ADDR, serialNo);
    if (gwData != NULL) {
        return gwDataHandler(req, gwData);
    } else {
        res.status = 404;
        return json::object({
            {"error", {
                {"status", res.status},
                {"message", "gateway with SN " + serialNo + " was not found"}
            }}
        });
    }
}

int main(int argc, char *argv[]) {
    /***
     * webserver on port 5900
     * library: https://github.com/yhirose/cpp-httplib
     */

    Server s;
    s.Get("/", [](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("This is the beginnings of a HART MUX web API!\n\n\
        Routes:\n\
        GET\t/\t\t\t\t\twelcome page\n\
        GET\t/gw/discover\t\t\t\tdiscover all gateways on the network\n\
        GET\t/gw/{serialNo}/info\t\t\tHART MUX info (including connected devices)\n\
        GET\t/gw/{serialNo}/info/{card}/{ch}\t\tread device info\n\
        GET\t/gw/{serialNo}/vars\t\t\tget list of all devices w/ their vars\n\
        GET\t/gw/{serialNo}/vars/{ioCard}\t\tget list of all devices on this io card w/ their vars\n\
        GET\t/gw/{serialNo}/vars/{card}/{ch}\t\tread vars from device\n", "text/plain");
    });

    s.Get("/gw/discover", [](const Request& req, Response& res) {
        json gwData = discoverGWs(BCAST_ADDR);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(gwData.dump(), "text/json");
    });

    // GET /gw/{serialNo}/info
    s.Get(R"(/gw/(\d+)/info)", [](const Request& req, Response& res) {
        string serialNo(req.matches[1]);
        json gws = discoverGWs(BCAST_ADDR);
        json gwData;
        for (int i=0; i<gws["gateways"].size(); i++) {
            gwData = gws["gateways"][i];
            if (gwData["serialNo"] == serialNo) break;
        }
        
        HartMux hart_mux(gwData["ip"]);
        hart_mux.initSession();
        hart_mux.autodiscoverSubDevices();
        hart_mux.closeSession();
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(hart_mux.to_json().dump(), "text/json");
    });

    // GET /gw/{serialNo}/info/{ioCard}/{channel}
    s.Get(R"(/gw/(\d+)/info/(\d+)/(\d+))", [](const Request& req, Response& res) {
        string serialNo(req.matches[1]);
        string ioCard(req.matches[2]);
        string channel(req.matches[3]);

        json gws = discoverGWs(BCAST_ADDR);
        json gwData;
        for (int i=0; i<gws["gateways"].size(); i++) {
            gwData = gws["gateways"][i];
            if (gwData["serialNo"] == serialNo) break;
        }
        HartMux hart_mux(gwData["ip"]);
        hart_mux.initSession();
        hart_mux.autodiscoverSubDevices();
        HartDevice dev;
        for (int i=0; i<hart_mux.ioCapabilities.numConnectedDevices; i++) {
            dev = hart_mux.devices[i];
            if (dev.ioCard == (uint8_t)stoi(ioCard) && dev.channel == (uint8_t)stoi(channel)) break;
        }

        hart_mux.closeSession();
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(dev.to_json().dump(), "text/json");
    });

    // GET /gw/{serialNo}/vars
    s.Get(R"(/gw/(\d+)/vars)", [](const Request& req, Response& res) {
        string serialNo(req.matches[1]);

        json gws = discoverGWs(BCAST_ADDR);
        json gwData;
        for (int i=0; i<gws["gateways"].size(); i++) {
            gwData = gws["gateways"][i];
            if (gwData["serialNo"] == serialNo) break;
        }
        HartMux hart_mux(gwData["ip"]);
        hart_mux.initSession();
        hart_mux.autodiscoverSubDevices();

        json data = json::object();
        data["devices"] = json::array();
        for (int i=0; i<hart_mux.ioCapabilities.numConnectedDevices; i++) {
            HartDevice dev = hart_mux.devices[i];
            cout << i+1 << "/" << hart_mux.ioCapabilities.numConnectedDevices << ": name=" << dev.name << "\taddr="; printBytes(dev.addrUniq, 5);
            long addr = 0;
            for (int i=0; i<sizeof(dev.addrUniq); i++) {
                addr += dev.addrUniq[i];
            }
            if (addr == 0) { // ISSUE: this workaround prevents hanging when the device address is null
                continue;
            }
            json vars = to_json(hart_mux.readSubDeviceVars(dev));
            data["devices"][i] = dev.to_json();
            data["devices"][i]["vars"] = vars;
        }

        hart_mux.closeSession();
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(data.dump(), "text/json");
    });

    // GET /gw/{serialNo}/vars/{ioCard}
    s.Get(R"(/gw/(\d+)/vars/(\d+))", [](const Request& req, Response& res) {
        string serialNo(req.matches[1]);
        string ioCard(req.matches[2]);

        json gws = discoverGWs(BCAST_ADDR);
        json gwData;
        for (int i=0; i<gws["gateways"].size(); i++) {
            gwData = gws["gateways"][i];
            if (gwData["serialNo"] == serialNo) break;
        }
        HartMux hart_mux(gwData["ip"]);
        hart_mux.initSession();
        hart_mux.autodiscoverSubDevices();

        json data = json::object();
        data["devices"] = json::array();
        int count = 0;
        for (int i=0; i<hart_mux.ioCapabilities.numConnectedDevices; i++) {
            HartDevice dev = hart_mux.devices[i];
            // cout << i+1 << "/" << hart_mux.ioCapabilities.numConnectedDevices << ": name=" << dev.name << "\taddr="; printBytes(dev.addrUniq, 5);
            long addr = 0;
            for (int i=0; i<sizeof(dev.addrUniq); i++) {
                addr += dev.addrUniq[i];
            }
            if (addr == 0) { // ISSUE: this workaround prevents hanging when the device address is null
                continue;
            }
            if (dev.ioCard == (uint8_t)stoi(ioCard)) {
                json vars = to_json(hart_mux.readSubDeviceVars(dev));
                data["devices"][count] = dev.to_json();
                data["devices"][count++]["vars"] = vars;
            }
        }

        hart_mux.closeSession();
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(data.dump(), "text/json");
    });

    // GET /gw/{serialNo}/vars/{ioCard}/{channel}
    s.Get(R"(/gw/(\d+)/vars/(\d+)/(\d+))", [](const Request& req, Response& res) {
        string serialNo(req.matches[1]);
        string ioCard(req.matches[2]);
        string channel(req.matches[3]);

        json gws = discoverGWs(BCAST_ADDR);
        json gwData;
        for (int i=0; i<gws["gateways"].size(); i++) {
            gwData = gws["gateways"][i];
            if (gwData["serialNo"] == serialNo) break;
        }
        HartMux hart_mux(gwData["ip"]);
        hart_mux.initSession();
        hart_mux.autodiscoverSubDevices();
        HartDevice dev;
        for (int i=0; i<hart_mux.ioCapabilities.numConnectedDevices; i++) {
            dev = hart_mux.devices[i];
            if (dev.ioCard == (uint8_t)stoi(ioCard) && dev.channel == (uint8_t)stoi(channel)) break;
        }

        hart_var_set var_set = hart_mux.readSubDeviceVars(dev);

        hart_mux.closeSession();
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(to_json(var_set).dump(), "text/json");
    });

    // GET /gw/{serialNo}/subdevice/{ioCard}/{channel}/cmd/{cmd}
    s.Get(R"(/gw/(\d+)/subdevice/(\d+)/(\d+)/cmd/(\d+))", [](const Request& req, Response& res) {
        // cout << "GET /gw/{serialNo}/subdevice/{ioCard}/{channel}/cmd/{cmd}" << endl;
        json data = with_gw_data_or_error(req, res, [](Request req, json gwData) {
            string ioCard(req.matches[2]);
            string channel(req.matches[3]);
            int cmd = stoi(req.matches[4]);

            HartMux hart_mux(gwData["ip"]);
            hart_mux.initSession();
            hart_mux.autodiscoverSubDevices();
            HartDevice dev;
            for (int i=0; i<hart_mux.ioCapabilities.numConnectedDevices; i++) {
                dev = hart_mux.devices[i];
                if (dev.ioCard == (uint8_t)stoi(ioCard) && dev.channel == (uint8_t)stoi(channel)) break;
            }

            uint8_t buf[512];
            size_t cnt;
            hart_mux.sendSubDeviceCmd(cmd, dev, nullptr, 0, buf, cnt);
            hart_mux.closeSession();

            json r = {
                {"response", bytesToHexStr(buf, cnt)}
            };
            return r;
        });
        
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(data.dump(), "text/json");
    });

    string domain = "0.0.0.0";
    unsigned int port = 5900;
    cout << "starting HART IP server at "+domain+":"<<port<<endl;
    s.listen(domain.c_str(), port);
    // end webserver

    return 0;
}