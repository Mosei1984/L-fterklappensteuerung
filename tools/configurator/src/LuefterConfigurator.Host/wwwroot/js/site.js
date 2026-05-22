(() => {
  const statusPill = document.querySelector("[data-status-pill]");
  const toast = document.querySelector("[data-toast]");
  const consoleEl = document.querySelector("[data-console]");
  const lastEvent = document.querySelector("[data-last-event]");
  const deviceList = document.querySelector("[data-device-list]");
  const controllerMetric = document.querySelector("[data-metric='controllers']");
  const activeIdMetric = document.querySelector("[data-metric='active-id']");
  const gatewayMetric = document.querySelector("[data-metric='gateway']");
  const statusLabel = document.querySelector("[data-status-label]");
  const nextStep = document.querySelector("[data-next-step]");
  const controllerId = document.querySelector("[data-controller-id]");
  const safeSlider = document.querySelector("[data-safe-slider]");
  const safeOutput = document.querySelector("[data-safe-output]");
  const safeChip = document.querySelector("[data-safe-chip]");
  const firmwareMode = document.querySelector("[data-firmware-mode]");
  const firmwareError = document.querySelector("[data-firmware-error]");
  const firmwareFlash = document.querySelector("[data-firmware-flash]");
  const firmwareFileInput = document.querySelector("[data-firmware-file]");
  const exportPath = document.querySelector("[data-export-path]");
  const exportFiles = document.querySelector("[data-export-files]");
  const wizardSteps = Array.from(document.querySelectorAll("[data-wizard-step]"));
  const wizardBack = document.querySelector("[data-wizard-back]");
  const wizardNext = document.querySelector("[data-wizard-next]");
  const wizardStatus = document.querySelector("[data-wizard-status]");
  const wizardHeading = document.querySelector("[data-wizard-heading]");
  const wizardCopy = document.querySelector("[data-wizard-copy]");

  const sampleProfile = `{
  "id": "fanflap-json",
  "displayName": "FanFlap JSON",
  "schemaVersion": "1.0",
  "transports": ["UsbText"],
  "settings": [
    {"key":"device.id","displayName":"Device ID","minimum":1,"maximum":247,"unit":"id"},
    {"key":"safe.position","displayName":"Safe Position","minimum":0,"maximum":1000,"unit":"promille"}
  ],
  "registers": [
    {"kind":"Holding","address":0,"name":"command","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":7,"name":"device_id","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":16,"name":"safe_position","valueType":"UInt16","access":"ReadWrite"}
  ]
}`;

  const showToast = (message) => {
    if (!toast) {
      return;
    }

    toast.textContent = message;
    toast.classList.add("visible");
    window.setTimeout(() => toast.classList.remove("visible"), 1800);
  };

  const wizardDetails = [
    {
      title: "Loxone vorbereiten",
      copy: "Gateway-IP, Port 5020 und die spaetere Controller-ID festlegen. Danach kann die Loxone-Importdatei exakt erzeugt werden."
    },
    {
      title: "Pico erkennen",
      copy: "USB verbinden, Portscan starten und vorhandene ID sowie Safe-Position passiv auslesen."
    },
    {
      title: "Firmware flashen",
      copy: "UF2-Modus pruefen, Firmwaredatei auswaehlen und Flashstatus im Wartungsbereich kontrollieren."
    },
    {
      title: "ID und Safe State schreiben",
      copy: "Controller-ID, Safe-Position und Softlimits setzen. Werte danach erneut auslesen."
    },
    {
      title: "Loxone Export herunterladen",
      copy: "Export testen erzeugt XML, JSON und Markdown. Die Dateien stehen direkt als Downloadlinks bereit."
    },
    {
      title: "Abschlusscheck",
      copy: "Statusregister lesen, Gateway pruefen und den sicheren Fehlerzustand fuer die Anlage dokumentieren."
    }
  ];

  let wizardIndex = 0;

  const updateWizard = () => {
    if (!wizardSteps.length) {
      return;
    }

    wizardSteps.forEach((step, index) => {
      step.classList.toggle("active", index === wizardIndex);
      step.setAttribute("aria-current", index === wizardIndex ? "step" : "false");
    });

    if (wizardBack) {
      wizardBack.disabled = wizardIndex === 0;
    }
    if (wizardNext) {
      wizardNext.textContent = wizardIndex === wizardSteps.length - 1 ? "Fertig" : "Weiter";
    }
    if (wizardStatus) {
      wizardStatus.textContent = `Schritt ${wizardIndex + 1} von ${wizardSteps.length}`;
    }
    if (wizardHeading && wizardDetails[wizardIndex]) {
      wizardHeading.textContent = wizardDetails[wizardIndex].title;
    }
    if (wizardCopy && wizardDetails[wizardIndex]) {
      wizardCopy.textContent = wizardDetails[wizardIndex].copy;
    }
  };

  const appendLog = (message) => {
    if (!consoleEl) {
      return;
    }

    const time = new Date().toLocaleTimeString("de-DE", {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit"
    });
    consoleEl.textContent += `\n> ${time} ${message}`;
    consoleEl.scrollTop = consoleEl.scrollHeight;
  };

  const escapeHtml = (value) => String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll("\"", "&quot;")
    .replaceAll("'", "&#39;");

  const setStatus = (isOnline) => {
    if (statusPill) {
      statusPill.classList.toggle("online", isOnline);
      statusPill.innerHTML = `<span aria-hidden="true"></span>${isOnline ? "Online" : "Offline"}`;
    }

    if (statusLabel) {
      statusLabel.textContent = isOnline ? "Online" : "Offline";
      statusLabel.classList.toggle("online", isOnline);
    }
  };

  const renderDevices = (controllers) => {
    if (!deviceList) {
      return;
    }

    if (!controllers.length) {
      deviceList.innerHTML = `
        <button class="device-row selected" type="button">
          <span class="device-dot pending" aria-hidden="true"></span>
          <span>
            <strong>Kein Controller verbunden</strong>
            <small>Scan starten oder USB verbinden</small>
          </span>
          <em>offline</em>
        </button>`;
      return;
    }

    deviceList.innerHTML = controllers.map((controller) => `
      <button class="device-row selected" type="button">
        <span class="device-dot online" aria-hidden="true"></span>
        <span>
          <strong>Controller ID ${controller.deviceId}</strong>
          <small>${controller.transportName}, ${controller.profileId}, Firmware ${controller.firmwareVersion}</small>
        </span>
        <em>${controller.isOnline ? "online" : "offline"}</em>
      </button>`).join("");
  };

  const applySnapshot = (snapshot) => {
    if (!snapshot) {
      return;
    }

    const controllers = Array.isArray(snapshot.controllers) ? snapshot.controllers : [];
    const activeId = snapshot.activeDeviceId ?? "-";
    const safePosition = snapshot.safePositionPromille ?? 250;
    const isOnline = controllers.some((controller) => controller.isOnline);

    setStatus(isOnline);
    renderDevices(controllers);

    if (controllerMetric) {
      controllerMetric.textContent = String(controllers.length);
    }
    if (activeIdMetric) {
      activeIdMetric.textContent = String(activeId);
    }
    if (gatewayMetric) {
      gatewayMetric.textContent = snapshot.gatewayRunning ? "Run" : "Stop";
    }
    if (safeOutput) {
      safeOutput.textContent = String(safePosition);
    }
    if (safeChip) {
      safeChip.textContent = `${safePosition} / 1000`;
    }
    if (safeSlider) {
      safeSlider.value = String(safePosition);
    }
    if (controllerId && snapshot.activeDeviceId) {
      controllerId.value = String(snapshot.activeDeviceId);
    }
    if (lastEvent) {
      lastEvent.textContent = snapshot.lastEvent;
    }
    if (nextStep) {
      nextStep.textContent = snapshot.lastEvent;
    }
    if (consoleEl && Array.isArray(snapshot.log)) {
      consoleEl.textContent = snapshot.log.join("\n");
      consoleEl.scrollTop = consoleEl.scrollHeight;
    }
    if (firmwareMode && snapshot.firmware) {
      firmwareMode.textContent = snapshot.firmware.driveRoot
        ? `${snapshot.firmware.mode} (${snapshot.firmware.driveRoot})`
        : snapshot.firmware.mode;
    }
    if (firmwareError && snapshot.firmware) {
      firmwareError.textContent = snapshot.firmware.error || "-";
      firmwareError.classList.toggle("error-text", Boolean(snapshot.firmware.error));
    }
    if (firmwareFlash && snapshot.firmware) {
      firmwareFlash.textContent = snapshot.firmware.lastFlashedFile
        ? `${snapshot.firmware.lastFlashedFile} (${snapshot.firmware.bytesWritten ?? 0} Bytes)`
        : "Keine Datei";
    }
    if (exportPath) {
      exportPath.textContent = snapshot.exportDirectory || "Noch nicht exportiert";
    }
    if (exportFiles) {
      const files = Array.isArray(snapshot.exportFiles) ? snapshot.exportFiles : [];
      exportFiles.innerHTML = files.length
        ? files.map((file) => {
            const fileName = escapeHtml(file.fileName);
            const url = `/api/exports/${encodeURIComponent(file.fileName)}`;
            return `<a href="${url}" download data-export-downloads>${fileName}<span>${escapeHtml(file.adapterId)}</span></a>`;
          }).join("")
        : `<span class="empty-export">Noch keine Exportdateien erzeugt</span>`;
    }
  };

  const handleResponsePayload = async (response) => {
    const text = await response.text();
    const payload = text ? JSON.parse(text) : null;

    if (payload?.snapshot) {
      applySnapshot(payload.snapshot);
    } else if (payload?.controllers) {
      applySnapshot(payload);
    }

    if (!response.ok) {
      throw new Error(payload?.error || payload?.message || response.statusText);
    }

    if (payload?.success === false && payload?.error) {
      showToast(payload.error);
    } else if (payload?.message) {
      showToast(payload.message);
    }

    return payload;
  };

  const requestJson = async (route, options = {}) => {
    const response = await fetch(route, {
      ...options,
      headers: {
        "Content-Type": "application/json",
        ...(options.headers || {})
      }
    });

    return handleResponsePayload(response);
  };

  const requestForm = async (route, formData) => {
    const response = await fetch(route, {
      method: "POST",
      body: formData
    });

    return handleResponsePayload(response);
  };

  const post = (route, body) => requestJson(route, {
    method: "POST",
    body: body === undefined ? undefined : JSON.stringify(body)
  });

  const actions = {
    scan: () => post("/api/controllers/scan"),
    connect: () => post("/api/controllers/connect"),
    import: () => post("/api/profiles/import", { json: sampleProfile }),
    "write-config": () => requestJson("/api/controllers/config", {
      method: "PUT",
      body: JSON.stringify({
        deviceId: Number(controllerId?.value || 1),
        safePositionPromille: Number(safeSlider?.value || 250)
      })
    }),
    home: () => post("/api/commands/home"),
    open: () => post("/api/commands/open"),
    half: () => post("/api/commands/half"),
    safe: () => post("/api/commands/safe"),
    "refresh-machine": () => post("/api/commands/refresh-machine"),
    gateway: () => post(gatewayMetric?.textContent === "Run" ? "/api/gateway/stop" : "/api/gateway/start"),
    firmware: () => post("/api/firmware/check"),
    "firmware-flash": () => {
      firmwareFileInput?.click();
      return Promise.resolve();
    },
    export: () => post("/api/exports")
  };

  document.addEventListener("click", async (event) => {
    const target = event.target.closest("[data-action]");
    if (!target) {
      return;
    }

    const action = target.getAttribute("data-action");
    if (!action || !actions[action]) {
      return;
    }

    target.setAttribute("aria-busy", "true");
    try {
      await actions[action]();
    } catch (error) {
      const message = error instanceof Error ? error.message : "Unbekannter Fehler";
      appendLog(`Fehler: ${message}`);
      showToast(message);
    } finally {
      target.removeAttribute("aria-busy");
    }
  });

  wizardBack?.addEventListener("click", () => {
    wizardIndex = Math.max(0, wizardIndex - 1);
    updateWizard();
  });

  wizardNext?.addEventListener("click", () => {
    if (!wizardSteps.length) {
      return;
    }

    if (wizardIndex >= wizardSteps.length - 1) {
      showToast("Loxone Setup bereit fuer Abschlusscheck");
      return;
    }

    wizardIndex += 1;
    updateWizard();
  });

  wizardSteps.forEach((step, index) => {
    step.addEventListener("click", () => {
      wizardIndex = index;
      updateWizard();
    });
  });

  safeSlider?.addEventListener("input", () => {
    if (safeOutput) {
      safeOutput.textContent = safeSlider.value;
    }
    if (safeChip) {
      safeChip.textContent = `${safeSlider.value} / 1000`;
    }
  });

  firmwareFileInput?.addEventListener("change", async () => {
    const file = firmwareFileInput.files?.[0];
    if (!file) {
      return;
    }

    const flashButton = document.querySelector("[data-action='firmware-flash']");
    const formData = new FormData();
    formData.append("file", file, file.name);
    flashButton?.setAttribute("aria-busy", "true");
    try {
      await requestForm("/api/firmware/flash", formData);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Firmware flashen fehlgeschlagen";
      appendLog(`Fehler: ${message}`);
      showToast(message);
    } finally {
      firmwareFileInput.value = "";
      flashButton?.removeAttribute("aria-busy");
    }
  });

  requestJson("/api/controllers/state").catch((error) => {
    const message = error instanceof Error ? error.message : "Statusabfrage fehlgeschlagen";
    appendLog(`Fehler: ${message}`);
  });

  updateWizard();
})();
