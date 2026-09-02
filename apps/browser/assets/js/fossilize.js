(function() {
    if (window.__sabreFossilizing) return;
    window.__sabreFossilizing = true;

    new QWebChannel(qt.webChannelTransport, function(channel) {
        const bridge = channel.objects.fossilBridge;
        if (!bridge) return;

        const fossilId = window.__currentFossilId; // Set by C++ before injection
        const selectors = 'img[src], link[rel="stylesheet"][href], script[src], video[src], audio[src], source[src]';
        const elements = document.querySelectorAll(selectors);

        let pending = elements.length;
        if (pending === 0) {
            finishAndSave();
            return;
        }

        elements.forEach(el => {
            const url = el.src || el.href;
            if (!url || url.startsWith('data:') || url.startsWith('blob:')) {
                pending--;
                if (pending === 0) finishAndSave();
                return;
            }

            // Use the browser's native fetch to bypass C++ network limitations
            fetch(url)
                .then(res => res.blob())
                .then(blob => {
                    const reader = new FileReader();
                    reader.onloadend = () => {
                        const base64 = reader.result.split(',')[1];
                        bridge.saveAsset(url, base64, blob.type);

                        // Rewrite the DOM to point to our custom scheme
                        const encodedUrl = encodeURIComponent(url);
                        const fossilUrl = `sabre-fossil://${fossilId}/${encodedUrl}`;
                        if (el.src) el.src = fossilUrl;
                        if (el.href) el.href = fossilUrl;

                        pending--;
                        if (pending === 0) finishAndSave();
                    };
                    reader.readAsDataURL(blob);
                })
                .catch(() => {
                    pending--;
                    if (pending === 0) finishAndSave();
                });
        });

        function finishAndSave() {
            const finalHtml = document.documentElement.outerHTML;
            bridge.saveHtml(finalHtml);
        }
    });
})();
