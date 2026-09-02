// apps/browser/qwebchannel.js
"use strict";
var QWebChannelMessageTypes = {
    signal: 1, propertyUpdate: 2, init: 3, idle: 4, debug: 5,
    invokeMethod: 6, connectToSignal: 7, disconnectFromSignal: 8, setProperty: 9, response: 10,
};

var QWebChannel = function(transport, initCallback) {
    if (typeof transport !== "object" || typeof transport.send !== "function") {
        console.error("QWebChannel expects a transport object with a send function.");
        return;
    }
    var channel = this;
    this.transport = transport;
    this.send = function(data) {
        if (typeof(data) !== "string") { data = JSON.stringify(data); }
        channel.transport.send(data);
    };
    this.transport.onmessage = function(message) {
        var data = JSON.parse(message.data);
        if (data.type === QWebChannelMessageTypes.signal) channel.handleSignal(data);
        else if (data.type === QWebChannelMessageTypes.response) channel.handleResponse(data);
        else if (data.type === QWebChannelMessageTypes.propertyUpdate) channel.handlePropertyUpdate(data);
    };
    this.execCallbacks = {}; this.execId = 0;
    this.exec = function(data, callback) {
        if (!callback) { channel.send(data); return; }
        if (channel.execId === Number.MAX_VALUE) channel.execId = Number.MIN_VALUE;
        channel.execId++; var messageId = channel.execId;
        channel.execCallbacks[messageId] = callback; data.id = messageId; channel.send(data);
    };
    this.handleResponse = function(data) {
        if (data.hasOwnProperty("id")) { channel.execCallbacks[data.id](data.data); delete channel.execCallbacks[data.id]; }
    };
    this.handleSignal = function(data) {
        if (channel.objects[data.object] && channel.objects[data.object][data.signal]) {
            channel.objects[data.object][data.signal].emit.apply(channel.objects[data.object][data.signal], data.args);
        }
    };
    this.handlePropertyUpdate = function(data) {
        for (var i in data.data) {
            var object = channel.objects[i];
            if (object) object.propertyUpdate(data.data[i].signals, data.data[i].properties);
        }
        channel.exec({type: QWebChannelMessageTypes.idle});
    };
    this.objects = {};
    this.createObject = function(objectName, objectData) {
        var obj = new QObject(objectName, objectData, channel);
        channel.objects[objectName] = obj; return obj;
    };
    this.handleInit = function(data) {
        for (var objectName in data.data) channel.createObject(objectName, data.data[objectName]);
        if (initCallback) initCallback(channel);
        channel.exec({type: QWebChannelMessageTypes.idle});
    };
    this.exec({type: QWebChannelMessageTypes.init}, function(response) { channel.handleInit(response); });
};

var QObject = function(name, data, webChannel) {
    this.__id__ = name; webChannel.objects[name] = this;
    this.__objectSignals__ = {}; this.__propertyCache__ = {};
    var object = this;
    this.unwrapQObject = function(response) {
        if (response instanceof Array) {
            var ret = new Array(response.length);
            for (var i = 0; i < response.length; ++i) ret[i] = object.unwrapQObject(response[i]);
            return ret;
        }
        if (!response || !response["__QObject*__"] || response.id === undefined) return response;
        var objectId = response.id;
        if (webChannel.objects[objectId]) return webChannel.objects[objectId];
        if (!response.data) return;
        var qObject = new QObject(objectId, response.data, webChannel);
        qObject.destroyed.connect(function() { if (webChannel.objects[objectId] === qObject) delete webChannel.objects[objectId]; });
        qObject.unwrapProperties(); return qObject;
    };
    this.unwrapProperties = function() {
        for (var propertyIdx in object.__propertyCache__) {
            object.__propertyCache__[propertyIdx] = object.unwrapQObject(object.__propertyCache__[propertyIdx]);
        }
    };
    function addSignal(signalData, isPropertyNotifySignal) {
        var signalName = signalData[0], signalIndex = signalData[1];
        object[signalName] = {
            connect: function(callback) {
                if (typeof(callback) !== "function") return;
                object.__objectSignals__[signalIndex] = object.__objectSignals__[signalIndex] || [];
                object.__objectSignals__[signalIndex].push(callback);
                if (!isPropertyNotifySignal && signalName !== "destroyed") {
                    webChannel.exec({type: QWebChannelMessageTypes.connectToSignal, object: object.__id__, signal: signalIndex});
                }
            },
            disconnect: function(callback) {
                if (typeof(callback) !== "function") return;
                object.__objectSignals__[signalIndex] = object.__objectSignals__[signalIndex] || [];
                var idx = object.__objectSignals__[signalIndex].indexOf(callback);
                if (idx !== -1) {
                    object.__objectSignals__[signalIndex].splice(idx, 1);
                    if (!isPropertyNotifySignal && object.__objectSignals__[signalIndex].length === 0) {
                        webChannel.exec({type: QWebChannelMessageTypes.disconnectFromSignal, object: object.__id__, signal: signalIndex});
 are: false;
        }
    };
    data.methods.forEach(addMethod);
    data.properties.forEach(bindGetterSetter);
    data.signals.forEach(function(signal) { addSignal(signal, false); });
    for (var name in data.enums) { object[name] = data.enums[name]; }
};

if (typeof module === 'object') { module.exports = { QWebChannel: QWebChannel }; }
