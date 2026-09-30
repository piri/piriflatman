#!/usr/bin/env python3
"""Alpaca HTTP/UDP checks. Read-only by default; --exercise changes device state."""
import argparse
import concurrent.futures
import http.client
import json
import socket
import sys
import time
import urllib.parse


def discover(target, port, timeout=1.5):
    found = set()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(timeout)
        sock.sendto(b"alpacadiscovery1", (target, port))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            sock.settimeout(max(0.01, deadline - time.monotonic()))
            try:
                packet, peer = sock.recvfrom(1024)
            except socket.timeout:
                break
            data = json.loads(packet)
            assert type(data["AlpacaPort"]) is int and 0 < data["AlpacaPort"] <= 65535
            found.add((peer[0], data["AlpacaPort"]))
    return found


class Client:
    def __init__(self, host, port):
        self.host, self.port, self.transaction = host, port, 0
        self.server_ids = set()
        self.checks = 0

    def raw(self, method, path, body=None, content_type="application/x-www-form-urlencoded"):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=7)
        try:
            headers = {"Content-Type": content_type} if body is not None else {}
            connection.request(method, path, body=body, headers=headers)
            response = connection.getresponse()
            return response.status, response.getheader("Content-Type"), response.read().decode()
        finally:
            connection.close()

    def call(self, member, method="GET", parameters=None, error=0, path=None):
        self.transaction += 1
        params = {"ClientID": 42, "ClientTransactionID": self.transaction}
        params.update(parameters or {})
        query = urllib.parse.urlencode(params)
        url = path or "/api/v1/covercalibrator/0/" + member
        status, media, body = self.raw(method, url + ("?" + query if method == "GET" else ""), query if method == "PUT" else None)
        assert status == 200, (url, status, body)
        assert media.startswith("application/json"), media
        doc = json.loads(body)
        assert doc["ClientTransactionID"] == self.transaction, doc
        assert type(doc["ServerTransactionID"]) is int and doc["ServerTransactionID"] > 0, doc
        assert doc["ServerTransactionID"] not in self.server_ids, doc
        self.server_ids.add(doc["ServerTransactionID"])
        assert doc["ErrorNumber"] == error, doc
        assert isinstance(doc["ErrorMessage"], str) and bool(doc["ErrorMessage"]) == bool(error), doc
        if error:
            assert "Value" not in doc
        self.checks += 1
        return doc.get("Value")

    def malformed(self, method, path, body=None, status=400):
        result = self.raw(method, path, body)
        assert result[0] == status and result[1].startswith("text/plain"), result
        self.checks += 1

    def exercise(self):
        base = "/api/v1/covercalibrator/0/"
        try:
            self.call("connected", "PUT", {"Connected": "false"})
            assert self.call("connected") is False
            for m in ("brightness", "maxbrightness", "calibratorstate", "coverstate", "calibratorchanging", "covermoving", "devicestate"):
                self.call(m, error=1031)
            self.call("calibratoron", "PUT", {"Brightness": 1}, error=1031)
            self.call("calibratoroff", "PUT", error=1031)
            self.call("connect", "PUT")
            deadline = time.monotonic() + 3
            while self.call("connecting"):
                assert time.monotonic() < deadline
                time.sleep(0.02)
            assert self.call("connected") is True
            assert self.call("calibratorstate") == 1
            assert self.call("brightness") == 0
            assert self.call("maxbrightness") == 1000
            assert self.call("coverstate") == 0
            assert self.call("covermoving") is False
            for m in ("opencover", "closecover", "haltcover"):
                self.call(m, "PUT", error=1024)
            self.call("action", "PUT", {"ActionName": "unknown", "Parameters": ""}, error=1024)
            for m in ("commandblind", "commandbool", "commandstring"):
                self.call(m, "PUT", {"Command": "foo", "Raw": "false"}, error=1024)
            for brightness in (0, 1, 100, 500, 1000):
                self.call("calibratoron", "PUT", {"Brightness": brightness})
                assert self.call("brightness") == brightness
                assert self.call("calibratorstate") == 3
                assert self.call("calibratorchanging") is False
            self.call("calibratoron", "PUT", {"Brightness": -1}, error=1025)
            self.call("calibratoron", "PUT", {"Brightness": 1001}, error=1025)
            assert self.call("brightness") == 1000
            state = {v["Name"]: v["Value"] for v in self.call("devicestate")}
            assert {k: state[k] for k in ("Brightness", "CalibratorState", "CalibratorChanging", "CoverState", "CoverMoving")} == {
                "Brightness": 1000, "CalibratorState": 3, "CalibratorChanging": False, "CoverState": 0, "CoverMoving": False}
            for body in ("", "brightness=2", "Brightness=no", "Brightness=1.5", "Brightness=2147483648", "Brightness=%", "Brightness=1&brightness=2"):
                self.malformed("PUT", base + "calibratoron", body)
            self.malformed("PUT", base + "connected", "Connected=yes")
            self.malformed("PUT", base + "calibratoron?Brightness=20", "ClientID=42")
            assert self.call("brightness") == 1000
            # Connection applies globally, independent of caller ID.
            self.call("calibratoron", "PUT", {"Brightness": 25, "ClientID": 99})
            self.call("disconnect", "PUT", {"ClientID": 99})
            time.sleep(0.02)
            assert self.call("connected") is False
            self.call("connected", "PUT", {"Connected": "TRUE"})
            assert self.call("calibratorstate") == 1
            assert self.call("brightness") == 0
        finally:
            # Best-effort cleanup; a failed test must never intentionally leave illumination on.
            for member, params in (("calibratoroff", {}), ("connected", {"Connected": "false"})):
                try:
                    query = urllib.parse.urlencode({"ClientID": 42, **params})
                    self.raw("PUT", base + member, query)
                except (OSError, http.client.HTTPException):
                    print("Cleanup could not reach device; verify light off locally.", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", help="Device address; omit to discover on the local broadcast domain")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--discovery-port", type=int, default=32227)
    parser.add_argument("--broadcast", default="255.255.255.255")
    parser.add_argument("--exercise", action="store_true", help="Connect, change brightness, then turn off/disconnect")
    parser.add_argument("--allow-hardware-output", action="store_true", help="Allow --exercise on a non-mock device")
    args = parser.parse_args()
    found = discover(args.host or args.broadcast, args.discovery_port)
    assert found, "No discovery response"
    print("Discovery:", ", ".join(f"{h}:{p}" for h, p in sorted(found)))
    if not args.host:
        assert len(found) == 1, "Multiple devices found; select one with --host and --port"
        args.host, args.port = next(iter(found))
    else:
        assert (socket.gethostbyname(args.host), args.port) in found, "Discovery port differs from --port"
    c = Client(args.host, args.port)
    versions = c.call("", path="/management/apiversions")
    assert versions == [1]
    description = c.call("", path="/management/v1/description")
    assert all(description[k] for k in ("ServerName", "Manufacturer", "ManufacturerVersion", "Location"))
    devices = c.call("", path="/management/v1/configureddevices")
    assert len(devices) == 1
    device = devices[0]
    assert device["DeviceName"] == "PiriFlatMan" and device["DeviceType"] == "CoverCalibrator" and device["DeviceNumber"] == 0
    assert device["UniqueID"]
    print("UniqueID:", device["UniqueID"])
    assert c.call("name") == "PiriFlatMan"
    assert c.call("interfaceversion") == 2
    assert c.call("supportedactions") == []
    assert len(c.call("description")) <= 64
    assert c.call("driverinfo") and c.call("driverversion")
    for path in ("/", "/setup", "/setup/v1/covercalibrator/0/setup"):
        status, media, page = c.raw("GET", path)
        assert status == 200 and media.startswith("text/html") and "PiriFlatMan" in page
        c.checks += 1
    is_mock = "mock" in c.call("driverinfo").lower()
    for path in ("/api/v1/covercalibrator/1/name", "/api/v2/covercalibrator/0/name", "/API/v1/covercalibrator/0/name", "/api/v1/covercalibrator/0/Name", "/management/v9/description"):
        c.malformed("GET", path)
    c.malformed("GET", "/setup/v1/covercalibrator/1/setup", status=403)
    c.malformed("GET", "/api/v1/covercalibrator/0/name?ClientID=-1")
    # Independent simultaneous clients, each handled by the same bounded socket server.
    def parallel_read(_):
        status, _, body = c.raw("GET", "/api/v1/covercalibrator/0/name")
        assert status == 200
        return json.loads(body)["ServerTransactionID"]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        ids = list(pool.map(parallel_read, range(12)))
    assert len(set(ids)) == len(ids)
    c.checks += 12
    if args.exercise:
        assert is_mock or args.allow_hardware_output, "Refusing illumination commands: device is not a mock. Explicit --allow-hardware-output required."
        c.exercise()
    print(f"PASS {c.checks} HTTP checks, UDP discovery; {'control exercised and output turned off' if args.exercise else 'read-only'}")
    print("This is a smoke test, not Conform Universal or NINA validation.")


if __name__ == "__main__":
    main()
