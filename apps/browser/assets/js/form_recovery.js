// form_recovery.js
(function() {
    if (window.__sabreFormRecoveryLoaded) return;
  window.__sabreFormRecoveryLoaded = true;
  if (typeof QWebChannel === 'undefined') {
      console.warn("[Sabre] QWebChannel not defined. Retrying...");
      setTimeout(arguments.callee, 100);
      return;
  }

    // Wait for QWebChannel to be ready
    new QWebChannel(qt.webChannelTransport, function(channel) {
        const bridge = channel.objects.formRecovery;
        if (!bridge) return;

        const currentUrl = window.location.href;
        const DEBOUNCE_MS = 500;
        let debounceTimers = {};

        // ── 1. RESTORE DATA ON PAGE LOAD ──────────────────────────────────────
        bridge.getFormsForPage(currentUrl, function(jsonStr) {
            if (!jsonStr || jsonStr === "{}") return;

            const savedForms = JSON.parse(jsonStr);
            const forms = document.querySelectorAll('form');

            forms.forEach((form, index) => {
                const formId = form.id || ('idx_' + index);
                const data = savedForms[formId];
                if (!data) return;

                // Populate fields
                for (const [fieldName, value] of Object.entries(data)) {
                    const el = form.querySelector(`[name="${fieldName}"]`) || form.querySelector(`#${fieldName}`);
                    if (el) {
                        if (el.type === 'checkbox' || el.type === 'radio') {
                            el.checked = (el.value === value);
                        } else {
                            el.value = value;
                        }
                        // Trigger input event so the website's own JS notices the change
                        el.dispatchEvent(new Event('input', { bubbles: true }));
                        el.dispatchEvent(new Event('change', { bubbles: true }));
                    }
                }

                // ── 2. AUTO-SYNC (The "Never-Die" Form Submission) ────────────
                bridge.hasPendingSubmit(currentUrl, formId, function(isPending) {
                    if (isPending && navigator.onLine) {
                        console.log("[Sabre] Auto-submitting recovered form:", formId);
                        bridge.clearPendingSubmit(currentUrl, formId);

                        // Find the submit button and click it
                        const submitBtn = form.querySelector('button[type="submit"], input[type="submit"]');
                        if (submitBtn) {
                            setTimeout(() => submitBtn.click(), 500); // Small delay to let DOM settle
                        } else {
                            form.requestSubmit(); // Modern fallback
                        }
                    }
                });
            });
        });

        // ── 3. LISTEN FOR CHANGES & SAVE ──────────────────────────────────────
        document.addEventListener('input', handleInput, true);
        document.addEventListener('change', handleInput, true);

        function handleInput(e) {
            const el = e.target;
            const form = el.closest('form');
            if (!form) return;

            const formId = form.id || ('idx_' + Array.from(document.querySelectorAll('form')).indexOf(form));

            // Debounce saving to avoid spamming the SQLite DB on every keystroke
            clearTimeout(debounceTimers[formId]);
            debounceTimers[formId] = setTimeout(() => {
                const formData = new FormData(form);
                const dataObj = {};
                for (let [key, val] of formData.entries()) {
                    dataObj[key] = val;
                }
                bridge.saveForm(currentUrl, formId, JSON.stringify(dataObj));
            }, DEBOUNCE_MS);
        }

        // ── 4. INTERCEPT SUBMISSIONS (For the Auto-Sync feature) ──────────────
        document.addEventListener('submit', function(e) {
            const form = e.target;
            if (!navigator.onLine) {
                e.preventDefault();
                const formId = form.id || ('idx_' + Array.from(document.querySelectorAll('form')).indexOf(form));
                console.log("[Sabre] Network down. Queuing form for auto-sync:", formId);

                // Save current state and mark as pending
                const formData = new FormData(form);
                const dataObj = {};
                for (let [key, val] of formData.entries()) dataObj[key] = val;

                bridge.saveForm(currentUrl, formId, JSON.stringify(dataObj));
                bridge.markPendingSubmit(currentUrl, formId);

                // Optional: Show a subtle UI toast to the user
                alert("Sabre: Connection lost. Your form is saved and will auto-submit when you're back online.");
            }
        }, true);
    });
})();
