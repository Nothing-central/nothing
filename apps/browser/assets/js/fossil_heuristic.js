(function() {
    if (window.__sabreHeuristicRan) return;
    window.__sabreHeuristicRan = true;

    new QWebChannel(qt.webChannelTransport, function(channel) {
        const bridge = channel.objects.fossilBridge;
        if (!bridge) return;

        const scripts = document.querySelectorAll('script').length;
        const textLength = document.body.innerText.length;
        const ratio = scripts / (textLength / 1000 + 1);

        let status = "static"; // 🌿
        if (ratio > 0.5) status = "dynamic"; // 🍂
        if (document.querySelector('#__next, #app, #root')) status = "webapp"; // 🍁

        bridge.reportHeuristic(status);
    });
})();
