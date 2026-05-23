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
  const softMinDegree = document.querySelector("[data-soft-min-degree]");
  const softMaxDegree = document.querySelector("[data-soft-max-degree]");
  const stallGuardThreshold = document.querySelector("[data-stallguard-threshold]");
  const safeOutput = document.querySelector("[data-safe-output]");
  const safeChip = document.querySelector("[data-safe-chip]");
  const valveSimulation = document.querySelector(".valve-simulation");
  const valvePercent = document.querySelector("[data-valve-percent]");
  const valveLabel = document.querySelector("[data-valve-label]");
  const firmwareMode = document.querySelector("[data-firmware-mode]");
  const firmwareError = document.querySelector("[data-firmware-error]");
  const firmwareFlash = document.querySelector("[data-firmware-flash]");
  const firmwareFileInput = document.querySelector("[data-firmware-file]");
  const exportPath = document.querySelector("[data-export-path]");
  const exportFiles = document.querySelector("[data-export-files]");
  const wizardSteps = Array.from(document.querySelectorAll("[data-wizard-step]"));
  const wizardBack = document.querySelector("[data-wizard-back]");
  const wizardNext = document.querySelector("[data-wizard-next]");
  const wizardAction = document.querySelector("[data-wizard-action]");
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
    {"key":"safe.position","displayName":"Sichere Stellung","minimum":0,"maximum":1000,"unit":"promille"},
    {"key":"soft.min.degree","displayName":"Min Winkel","minimum":0,"maximum":90,"unit":"degree"},
    {"key":"soft.max.degree","displayName":"Max Winkel","minimum":0,"maximum":90,"unit":"degree"},
    {"key":"stallguard.threshold","displayName":"StallGuard Threshold","minimum":0,"maximum":255,"unit":"sgthrs"}
  ],
  "registers": [
    {"kind":"Holding","address":0,"name":"command","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":7,"name":"device_id","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":16,"name":"safe_position_promille","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":22,"name":"firmware_protocol_version","valueType":"UInt16","access":"ReadOnly"},
    {"kind":"Holding","address":23,"name":"target_degree","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":24,"name":"current_degree","valueType":"UInt16","access":"ReadOnly"},
    {"kind":"Holding","address":25,"name":"soft_min_degree","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":26,"name":"soft_max_degree","valueType":"UInt16","access":"ReadWrite"},
    {"kind":"Holding","address":27,"name":"stallguard_threshold","valueType":"UInt16","access":"ReadWrite"}
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
      copy: "Gateway-IP, Port 5020 und die spaetere Controller-ID festlegen. Danach fuehrt der Wizard direkt zur Pico-Suche.",
      actionLabel: "Weiter",
      action: null
    },
    {
      title: "Pico erkennen",
      copy: "Pico per USB anschliessen, Suche starten und vorhandene ID sowie sichere Stellung passiv auslesen.",
      actionLabel: "Pico suchen",
      action: "scan"
    },
    {
      title: "Firmware flashen",
      copy: "UF2-Modus pruefen. Wenn der Pico schon die richtige Firmware meldet, kann dieser Schritt uebersprungen werden.",
      actionLabel: "UF2 pruefen",
      action: "firmware"
    },
    {
      title: "ID, Winkel und StallGuard schreiben",
      copy: "Controller-ID, sichere Stellung, Grad-Limits und StallGuard-Schwelle setzen. Der Wizard schreibt nur die sichtbaren Werte.",
      actionLabel: "Werte schreiben",
      action: "write-config"
    },
    {
      title: "Loxone Export herunterladen",
      copy: "Export erzeugen erstellt Loxone XML, JSON und Markdown. Die Dateien erscheinen direkt als Downloadlinks.",
      actionLabel: "Export erzeugen",
      action: "export"
    },
    {
      title: "Abschlusscheck",
      copy: "Pico noch einmal suchen, Online-Status und sichere Stellung kontrollieren und die Anlage dokumentieren.",
      actionLabel: "Abschluss pruefen",
      action: "scan"
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
      wizardNext.textContent = wizardIndex === wizardSteps.length - 1 ? "Fertig" : "Ueberspringen";
      wizardNext.classList.toggle("is-hidden", !wizardDetails[wizardIndex]?.action);
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
    if (wizardAction && wizardDetails[wizardIndex]) {
      wizardAction.textContent = wizardDetails[wizardIndex].actionLabel;
      wizardAction.setAttribute("aria-label", `${wizardDetails[wizardIndex].actionLabel}: Schritt ${wizardIndex + 1}`);
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

  const clampPermille = (value) => Math.min(1000, Math.max(0, Number.isFinite(value) ? value : 0));

  const updateValveSimulation = (permille, label = "Sichere Stellung") => {
    const safePermille = clampPermille(Number(permille));
    const percent = Math.round(safePermille / 10);
    const angle = 90 - (safePermille / 1000) * 90;

    valveSimulation?.style.setProperty("--valve-angle", `${angle.toFixed(1)}deg`);
    if (valvePercent) {
      valvePercent.textContent = `${percent}%`;
    }
    if (valveLabel) {
      valveLabel.textContent = label;
    }
  };

  const setValveSimulation = (action) => {
    switch (action) {
      case "open":
        updateValveSimulation(1000, "Ganz offen");
        break;
      case "half":
        updateValveSimulation(500, "Halb offen");
        break;
      case "safe":
      case "write-config":
      case "scan":
        updateValveSimulation(Number(safeSlider?.value || 250), "Sichere Stellung");
        break;
      case "home":
        updateValveSimulation(0, "Referenzfahrt");
        break;
      case "refresh-machine":
        updateValveSimulation(Number(safeSlider?.value || 250), "Neu referenziert");
        break;
      case "step-test":
        updateValveSimulation(Number(safeSlider?.value || 250), "Stepper Test");
        break;
      default:
        break;
    }
  };

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
            <small>Pico per USB anschliessen und Suche starten</small>
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
    const minDegree = snapshot.softMinDegree ?? 0;
    const maxDegree = snapshot.softMaxDegree ?? 90;
    const stallGuard = snapshot.stallGuardThreshold ?? 100;
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
    if (softMinDegree) {
      softMinDegree.value = String(minDegree);
    }
    if (softMaxDegree) {
      softMaxDegree.value = String(maxDegree);
    }
    if (stallGuardThreshold) {
      stallGuardThreshold.value = String(stallGuard);
    }
    updateValveSimulation(safePosition, "Sichere Stellung");
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
        safePositionPromille: Number(safeSlider?.value || 250),
        softMinDegree: Number(softMinDegree?.value || 0),
        softMaxDegree: Number(softMaxDegree?.value || 90),
        stallGuardThreshold: Number(stallGuardThreshold?.value || 100)
      })
    }),
    home: () => post("/api/commands/home"),
    open: () => post("/api/commands/open"),
    half: () => post("/api/commands/half"),
    safe: () => post("/api/commands/safe"),
    "refresh-machine": () => post("/api/commands/refresh-machine"),
    "step-test": () => post("/api/commands/step-test"),
    gateway: () => post(gatewayMetric?.textContent === "Run" ? "/api/gateway/stop" : "/api/gateway/start"),
    firmware: () => post("/api/firmware/check"),
    "firmware-flash": () => {
      firmwareFileInput?.click();
      return Promise.resolve();
    },
    export: () => post("/api/exports")
  };

  const advanceWizard = () => {
    if (!wizardSteps.length) {
      return;
    }

    if (wizardIndex >= wizardSteps.length - 1) {
      showToast("Loxone Setup bereit fuer Abschlusscheck");
      return;
    }

    wizardIndex += 1;
    updateWizard();
  };

  const runAction = async (action, target) => {
    target?.setAttribute("aria-busy", "true");
    try {
      await actions[action]();
      setValveSimulation(action);
      return true;
    } catch (error) {
      const message = error instanceof Error ? error.message : "Unbekannter Fehler";
      appendLog(`Fehler: ${message}`);
      showToast(message);
      return false;
    } finally {
      target?.removeAttribute("aria-busy");
    }
  };

  const runWizardAction = async () => {
    const details = wizardDetails[wizardIndex];
    if (!details) {
      return;
    }

    if (!details.action) {
      advanceWizard();
      return;
    }

    const succeeded = await runAction(details.action, wizardAction);
    if (!succeeded) {
      return;
    }

    if (wizardIndex >= wizardSteps.length - 1) {
      showToast("Abschlusscheck aktualisiert");
      return;
    }

    advanceWizard();
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

    await runAction(action, target);
  });

  wizardBack?.addEventListener("click", () => {
    wizardIndex = Math.max(0, wizardIndex - 1);
    updateWizard();
  });

  wizardNext?.addEventListener("click", () => {
    advanceWizard();
  });

  wizardAction?.addEventListener("click", runWizardAction);

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
    updateValveSimulation(Number(safeSlider.value), "Sichere Stellung");
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
