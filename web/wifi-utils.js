(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    if (root) root.WifiUtils = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    const AUTH = {
        0: { label: 'Open', secure: false, supported: true },
        1: { label: 'WEP', secure: true, supported: false },
        2: { label: 'WPA-PSK', secure: true, supported: true },
        3: { label: 'WPA2-PSK', secure: true, supported: true },
        4: { label: 'WPA/WPA2-PSK', secure: true, supported: true },
        5: { label: 'WPA2-Enterprise', secure: true, supported: false },
        6: { label: 'WPA3-PSK', secure: true, supported: true },
        7: { label: 'WPA2/WPA3-PSK', secure: true, supported: true },
        8: { label: 'WAPI-PSK', secure: true, supported: false },
        9: { label: 'OWE', secure: false, supported: true },
        10: { label: 'WPA3-Enterprise 192-bit', secure: true, supported: false }
    };

    function authInfo(mode) {
        const numeric = Number(mode);
        return AUTH[numeric] || { label: `Security ${numeric}`, secure: true, supported: false };
    }

    function parseIpv4(text) {
        const parts = String(text || '').trim().split('.');
        if (parts.length !== 4) return null;
        let value = 0;
        for (const part of parts) {
            if (!/^\d{1,3}$/.test(part)) return null;
            const octet = Number(part);
            if (octet < 0 || octet > 255) return null;
            value = (value * 256) + octet;
        }
        return value >>> 0;
    }

    function isContiguousNetmask(text) {
        const mask = parseIpv4(text);
        if (mask === null || mask === 0 || mask === 0xffffffff) return false;
        const inverted = (~mask) >>> 0;
        return ((inverted + 1) & inverted) === 0;
    }

    function sameSubnet(ipText, gatewayText, maskText) {
        const ip = parseIpv4(ipText);
        const gateway = parseIpv4(gatewayText);
        const mask = parseIpv4(maskText);
        if (ip === null || gateway === null || mask === null) return false;
        return (ip & mask) === (gateway & mask);
    }

    function isUsableHost(ipText, maskText) {
        const ip = parseIpv4(ipText);
        const mask = parseIpv4(maskText);
        if (ip === null || mask === null || !isContiguousNetmask(maskText)) return false;
        const network = (ip & mask) >>> 0;
        const broadcast = (network | ((~mask) >>> 0)) >>> 0;
        return ip !== network && ip !== broadcast;
    }

    function validateStaticProfile(profile) {
        const errors = [];
        if (Number(profile.ip_mode) !== 1) return errors;
        if (parseIpv4(profile.static_ip) === null) errors.push(['ip', 'Enter a valid static IPv4 address.']);
        if (parseIpv4(profile.gateway) === null) errors.push(['gateway', 'Enter a valid IPv4 gateway.']);
        if (!isContiguousNetmask(profile.netmask)) errors.push(['netmask', 'Enter a contiguous IPv4 netmask.']);
        if (errors.length === 0) {
            if (!sameSubnet(profile.static_ip, profile.gateway, profile.netmask)) errors.push(['gateway', 'Gateway must be in the same subnet as the controller.']);
            if (!isUsableHost(profile.static_ip, profile.netmask)) errors.push(['ip', 'Static IP cannot be the network or broadcast address.']);
            if (!isUsableHost(profile.gateway, profile.netmask)) errors.push(['gateway', 'Gateway cannot be the network or broadcast address.']);
            if (parseIpv4(profile.static_ip) === parseIpv4(profile.gateway)) errors.push(['gateway', 'Gateway and controller IP must be different.']);
        }
        for (const key of ['dns1', 'dns2']) {
            if (profile[key] && parseIpv4(profile[key]) === null) errors.push([key, 'Enter a valid IPv4 address or leave blank.']);
        }
        return errors;
    }

    function signalLevel(rssi) {
        const value = Number(rssi);
        if (!Number.isFinite(value)) return 0;
        if (value >= -55) return 4;
        if (value >= -67) return 3;
        if (value >= -75) return 2;
        if (value >= -85) return 1;
        return 0;
    }

    return {
        authInfo,
        isContiguousNetmask,
        isUsableHost,
        parseIpv4,
        sameSubnet,
        signalLevel,
        validateStaticProfile
    };
});
