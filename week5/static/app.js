// Sample JSON response provided for standalone offline testing
const SAMPLE_RESPONSE = {
  "ok": true,
  "reference": [7, 0, 1, 2, 0, 3, 0, 4, 2, 3],
  "frames": 3,
  "results": [
    {
      "algorithm": "FIFO", "total": 10, "hits": 1, "faults": 9,
      "hit_ratio": 10.00, "fault_ratio": 90.00,
      "steps": [
        {"page": 7, "frames": [7, null, null], "hit": false, "replaced": null},
        {"page": 0, "frames": [7, 0, null],    "hit": false, "replaced": null},
        {"page": 1, "frames": [7, 0, 1],       "hit": false, "replaced": null},
        {"page": 2, "frames": [2, 0, 1],       "hit": false, "replaced": 7},
        {"page": 0, "frames": [2, 0, 1],       "hit": true,  "replaced": null},
        {"page": 3, "frames": [2, 3, 1],       "hit": false, "replaced": 0},
        {"page": 0, "frames": [2, 3, 0],       "hit": false, "replaced": 1},
        {"page": 4, "frames": [4, 3, 0],       "hit": false, "replaced": 2},
        {"page": 2, "frames": [4, 2, 0],       "hit": false, "replaced": 3},
        {"page": 3, "frames": [4, 2, 3],       "hit": false, "replaced": 0}
      ]
    },
    {
      "algorithm": "LRU", "total": 10, "hits": 2, "faults": 8,
      "hit_ratio": 20.00, "fault_ratio": 80.00,
      "steps": [
        {"page": 7, "frames": [7, null, null], "hit": false, "replaced": null},
        {"page": 0, "frames": [7, 0, null],    "hit": false, "replaced": null},
        {"page": 1, "frames": [7, 0, 1],       "hit": false, "replaced": null},
        {"page": 2, "frames": [2, 0, 1],       "hit": false, "replaced": 7},
        {"page": 0, "frames": [2, 0, 1],       "hit": true,  "replaced": null},
        {"page": 3, "frames": [2, 0, 3],       "hit": false, "replaced": 1},
        {"page": 0, "frames": [2, 0, 3],       "hit": true,  "replaced": null},
        {"page": 4, "frames": [4, 0, 3],       "hit": false, "replaced": 2},
        {"page": 2, "frames": [4, 0, 2],       "hit": false, "replaced": 3},
        {"page": 3, "frames": [4, 3, 2],       "hit": false, "replaced": 0}
      ]
    },
    {
      "algorithm": "Optimal", "total": 10, "hits": 4, "faults": 6,
      "hit_ratio": 40.00, "fault_ratio": 60.00,
      "steps": [
        {"page": 7, "frames": [7, null, null], "hit": false, "replaced": null},
        {"page": 0, "frames": [7, 0, null],    "hit": false, "replaced": null},
        {"page": 1, "frames": [7, 0, 1],       "hit": false, "replaced": null},
        {"page": 2, "frames": [2, 0, 1],       "hit": false, "replaced": 7},
        {"page": 0, "frames": [2, 0, 1],       "hit": true,  "replaced": null},
        {"page": 3, "frames": [2, 0, 3],       "hit": false, "replaced": 1},
        {"page": 0, "frames": [2, 0, 3],       "hit": true,  "replaced": null},
        {"page": 4, "frames": [2, 0, 4],       "hit": false, "replaced": 3},
        {"page": 2, "frames": [2, 0, 4],       "hit": true,  "replaced": null},
        {"page": 3, "frames": [3, 0, 4],       "hit": false, "replaced": 2}
      ]
    }
  ],
  "comparison": {"best": ["Optimal"], "min_faults": 6}
};

// Initialize event listeners when DOM loads
document.addEventListener("DOMContentLoaded", function () {
  const btnSimulate = document.getElementById("btn-simulate");
  const btnSample = document.getElementById("btn-sample");
  const btnClear = document.getElementById("btn-clear");
  const refInput = document.getElementById("reference-input");

  btnSimulate.addEventListener("click", handleSimulate);
  btnSample.addEventListener("click", loadSampleInput);
  btnClear.addEventListener("click", clearAll);

  // Trigger simulation on pressing Enter inside reference input
  refInput.addEventListener("keydown", function (e) {
    if (e.key === "Enter") {
      handleSimulate();
    }
  });
});

// Safely escape strings for HTML rendering
function escapeHtml(str) {
  if (str === null || str === undefined) return "";
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

// Display error message in the red error box
function showError(msg) {
  const errBox = document.getElementById("error-box");
  if (msg) {
    errBox.textContent = msg;
    errBox.classList.remove("hidden");
  } else {
    errBox.textContent = "";
    errBox.classList.add("hidden");
  }
}

// Gather user input values from the controls
function collectInput() {
  const reference = document.getElementById("reference-input").value.trim();
  const framesVal = document.getElementById("frames-input").value.trim();
  const selectedRadio = document.querySelector('input[name="algorithm"]:checked');
  const algorithm = selectedRadio ? selectedRadio.value : "all";

  return {
    reference: reference,
    frames: framesVal,
    algorithm: algorithm
  };
}

// Validate collect input prior to server dispatch
function validateInput(inputData) {
  if (!inputData.reference) {
    showError("Reference string is empty.");
    return false;
  }

  const framesNum = Number(inputData.frames);
  if (!inputData.frames || isNaN(framesNum) || !Number.isInteger(framesNum) || framesNum < 1 || framesNum > 10) {
    showError("Number of frames must be a whole number from 1 to 10.");
    return false;
  }

  showError(null);
  return true;
}

// Main handler for Simulate button
function handleSimulate() {
  const inputData = collectInput();
  if (!validateInput(inputData)) return;

  const btnSimulate = document.getElementById("btn-simulate");
  btnSimulate.disabled = true;
  btnSimulate.textContent = "Simulating...";

  sendRequest({
    reference: inputData.reference,
    frames: parseInt(inputData.frames, 10),
    algorithm: inputData.algorithm
  });
}

// Send POST payload to server
function sendRequest(payload) {
  fetch("/api/simulate", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  })
    .then(function (response) {
      return response.json().then(function (data) {
        return { status: response.status, data: data };
      });
    })
    .then(function (res) {
      if (!res.data.ok || res.status >= 400) {
        const errMsg = res.data.error || "Server returned an error (" + res.status + ")";
        showError(errMsg);
      } else {
        renderResults(res.data);
      }
    })
    .catch(function () {
      showError("Could not reach the server. Please verify the backend is running.");
    })
    .finally(function () {
      const btnSimulate = document.getElementById("btn-simulate");
      btnSimulate.disabled = false;
      btnSimulate.textContent = "Simulate";
    });
}

// Populate sample data into input controls
function loadSampleInput() {
  document.getElementById("reference-input").value = "7 0 1 2 0 3 0 4 2 3";
  document.getElementById("frames-input").value = "3";
  const allRadio = document.querySelector('input[name="algorithm"][value="all"]');
  if (allRadio) allRadio.checked = true;
  showError(null);
}

// Clear input fields and results
function clearAll() {
  document.getElementById("reference-input").value = "";
  document.getElementById("frames-input").value = "3";
  const allRadio = document.querySelector('input[name="algorithm"][value="all"]');
  if (allRadio) allRadio.checked = true;
  document.getElementById("results-container").innerHTML = "";
  showError(null);
}

// Helper to run offline testing via browser console
function useSampleData() {
  renderResults(SAMPLE_RESPONSE);
}

// Render complete simulation results
function renderResults(data) {
  const container = document.getElementById("results-container");
  container.innerHTML = "";

  if (!data.results || data.results.length === 0) return;

  const frameCount = data.frames;

  // Render individual algorithm cards
  data.results.forEach(function (result) {
    const card = document.createElement("section");
    card.className = "card";

    const title = document.createElement("h2");
    title.textContent = result.algorithm;
    card.appendChild(title);

    const stepTable = renderStepTable(result.steps, frameCount);
    card.appendChild(stepTable);

    const summary = renderSummary(result);
    card.appendChild(summary);

    container.appendChild(card);
  });

  // Render comparison card if multiple results exist
  if (data.results.length > 1 && data.comparison) {
    const compCard = document.createElement("section");
    compCard.className = "card";

    const compTitle = document.createElement("h2");
    compTitle.textContent = "Comparison";
    compCard.appendChild(compTitle);

    const compContent = renderComparison(data.results, data.comparison);
    compCard.appendChild(compContent);

    const chart = renderChart(data.results, data.comparison.best);
    compCard.appendChild(chart);

    container.appendChild(compCard);
  }
}

// Render step-by-step frame table
function renderStepTable(steps, frameCount) {
  const wrapper = document.createElement("div");
  wrapper.className = "table-wrapper";

  const table = document.createElement("table");
  table.className = "data-table";

  // Build header row
  let headerHtml = "<thead><tr><th>Step</th><th>Page</th>";
  for (let i = 1; i <= frameCount; i++) {
    headerHtml += "<th>Frame " + i + "</th>";
  }
  headerHtml += "<th>Status</th></tr></thead>";

  // Build body rows
  let bodyHtml = "<tbody>";
  steps.forEach(function (step, index) {
    const isHit = step.hit;
    const rowClass = isHit ? "row-hit" : "row-fault";
    const statusText = isHit ? "Hit" : "Fault";

    bodyHtml += '<tr class="' + rowClass + '">';
    bodyHtml += "<td>" + (index + 1) + "</td>";
    bodyHtml += "<td><strong>" + escapeHtml(step.page) + "</strong></td>";

    // Build frame cells
    for (let f = 0; f < frameCount; f++) {
      const val = (step.frames && step.frames[f] !== null && step.frames[f] !== undefined)
        ? step.frames[f]
        : "-";

      // Highlight replaced page frame on fault
      let isReplacedCell = false;
      if (!isHit && step.replaced !== null && step.replaced !== undefined) {
        if (step.frames[f] === step.page) {
          isReplacedCell = true;
        }
      }

      const cellClass = isReplacedCell ? ' class="cell-highlight"' : '';
      bodyHtml += "<td" + cellClass + ">" + escapeHtml(val) + "</td>";
    }

    // Build status cell with replacement details
    bodyHtml += '<td><span class="status-tag">' + statusText + "</span>";
    if (!isHit && step.replaced !== null && step.replaced !== undefined) {
      bodyHtml += '<span class="replaced-note">(replaced ' + escapeHtml(step.replaced) + ")</span>";
    }
    bodyHtml += "</td></tr>";
  });

  bodyHtml += "</tbody>";
  table.innerHTML = headerHtml + bodyHtml;
  wrapper.appendChild(table);
  return wrapper;
}

// Render metrics summary card footer
function renderSummary(result) {
  const summaryDiv = document.createElement("div");
  summaryDiv.className = "summary-grid";

  const items = [
    { label: "Total References", value: result.total },
    { label: "Page Hits", value: result.hits },
    { label: "Page Faults", value: result.faults },
    { label: "Hit Ratio", value: result.hit_ratio.toFixed(2) + "%" },
    { label: "Fault Ratio", value: result.fault_ratio.toFixed(2) + "%" }
  ];

  items.forEach(function (item) {
    const div = document.createElement("div");
    div.className = "summary-item";

    const lbl = document.createElement("span");
    lbl.className = "summary-label";
    lbl.textContent = item.label;

    const val = document.createElement("span");
    val.className = "summary-value";
    val.textContent = item.value;

    div.appendChild(lbl);
    div.appendChild(val);
    summaryDiv.appendChild(div);
  });

  return summaryDiv;
}

// Render Comparison table and statement text
function renderComparison(results, comparison) {
  const container = document.createElement("div");

  // Format best summary sentence
  const bestText = document.createElement("p");
  bestText.className = "best-summary-text";

  const bestList = comparison.best || [];
  const minFaults = comparison.min_faults;

  if (bestList.length > 1) {
    bestText.textContent = "Best (tie): " + bestList.join(", ") + " with " + minFaults + " page faults";
  } else if (bestList.length === 1) {
    bestText.textContent = "Best: " + bestList[0] + " with " + minFaults + " page faults";
  }
  container.appendChild(bestText);

  // Build comparison table
  const wrapper = document.createElement("div");
  wrapper.className = "table-wrapper";

  const table = document.createElement("table");
  table.className = "data-table";

  let html = "<thead><tr>" +
    "<th>Algorithm</th><th>Page Hits</th><th>Page Faults</th><th>Hit Ratio</th><th>Fault Ratio</th>" +
    "</tr></thead><tbody>";

  results.forEach(function (r) {
    const isBest = bestList.includes(r.algorithm);
    const rowClass = isBest ? ' class="best-row"' : '';

    html += "<tr" + rowClass + ">";
    html += "<td><strong>" + escapeHtml(r.algorithm) + "</strong>";
    if (isBest) {
      html += ' <span class="badge-best">Best</span>';
    }
    html += "</td>";
    html += "<td>" + escapeHtml(r.hits) + "</td>";
    html += "<td>" + escapeHtml(r.faults) + "</td>";
    html += "<td>" + escapeHtml(r.hit_ratio.toFixed(2)) + "%</td>";
    html += "<td>" + escapeHtml(r.fault_ratio.toFixed(2)) + "%</td>";
    html += "</tr>";
  });

  html += "</tbody>";
  table.innerHTML = html;
  wrapper.appendChild(table);
  container.appendChild(wrapper);

  return container;
}

// Render SVG bar chart for page faults
function renderChart(results, bestList) {
  const container = document.createElement("div");
  container.className = "chart-container";

  const chartTitle = document.createElement("h3");
  chartTitle.textContent = "Page Faults by Algorithm";
  container.appendChild(chartTitle);

  const svgNS = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(svgNS, "svg");
  svg.setAttribute("class", "chart-svg");
  svg.setAttribute("viewBox", "0 0 400 180");

  // Determine scale
  let maxFaults = 0;
  results.forEach(function (r) {
    if (r.faults > maxFaults) maxFaults = r.faults;
  });
  if (maxFaults === 0) maxFaults = 1;

  const count = results.length;
  const chartHeight = 120;
  const startY = 140;
  const barWidth = Math.min(60, 240 / count);
  const gap = (360 - count * barWidth) / (count + 1);

  results.forEach(function (r, index) {
    const x = gap + index * (barWidth + gap) + 20;
    const h = (r.faults / maxFaults) * chartHeight;
    const y = startY - h;

    const isBest = (bestList || []).includes(r.algorithm);

    // Draw rect bar
    const rect = document.createElementNS(svgNS, "rect");
    rect.setAttribute("x", x);
    rect.setAttribute("y", y);
    rect.setAttribute("width", barWidth);
    rect.setAttribute("height", h);
    rect.setAttribute("class", isBest ? "chart-bar chart-bar-best" : "chart-bar");
    svg.appendChild(rect);

    // Fault count label over bar
    const valText = document.createElementNS(svgNS, "text");
    valText.setAttribute("x", x + barWidth / 2);
    valText.setAttribute("y", Math.max(y - 6, 15));
    valText.setAttribute("class", "chart-value-text");
    valText.textContent = r.faults;
    svg.appendChild(valText);

    // Algorithm label below bar
    const labelText = document.createElementNS(svgNS, "text");
    labelText.setAttribute("x", x + barWidth / 2);
    labelText.setAttribute("y", startY + 20);
    labelText.setAttribute("class", "chart-text");
    labelText.textContent = r.algorithm;
    svg.appendChild(labelText);
  });

  container.appendChild(svg);
  return container;
}
