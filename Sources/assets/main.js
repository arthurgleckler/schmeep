const SCHEME_EXPRESSIONS = [
  "(lambda (x) (+ x 1))",
  "(define (factorial n) (if (<= n 1) 1 (* n (factorial (- n 1)))))",
  "(+ 1 2 3 4 5)",
  "(* 6 7)",
  "(sqrt 64)",
  "(expt 2 8)",
  "(abs -42)",
  "(min 5 3 8 1 9)",
  "(/ 1 0)",
  "(factorial 10)",
  "(import (srfi 27))",
  "(random-integer 10)",
  "(display \"foxtrot golf hotel\\n\") 42"
];

function completeEvaluation(expression, result, type) {
  const li = document.querySelector("#scheme-content ul li:last-child");

  if (li) {
    const span = li.querySelector(".value");

    if (span) {
      span.textContent = result;
      li.classList.remove("queued");
      li.classList.add(type);
    }
  }
}

function displayBluetoothExpression(expression) {
  displayExpressionWithSpans(" " + expression, "bluetooth", "queued");
}

function displayBluetoothResult(expression, result) {
  completeEvaluation(expression, result, "remote");
}

window.nativeDisplayExpression = function(expression) {
  displayExpressionWithSpans(" " + expression, "local", "local");
};

window.nativeDisplayResult = function(text, source, type) {
  displayResult(text, source, type);
};

window.nativeDisplayCapturedOutput = function(output) {
  if (output && output.trim()) {
    const li = document.querySelector("#scheme-content ul li:last-child");

    if (li) {
      const outputSpan = li.querySelector(".output");

      if (outputSpan) {
        outputSpan.textContent = output;
      }
    }
  }
};

function displayExpressionWithSpans(expression, source, type) {
  const schemeContent = document.getElementById("scheme-content");
  const ul = schemeContent.querySelector("ul");
  const li = document.createElement("li");

  li.classList.add("icon", source, type);

  const outputSpan = document.createElement("span");

  outputSpan.className = "output";

  const arrow = document.createTextNode(" ⇒ ");
  const span = document.createElement("span");

  span.className = "value";
  span.textContent = "...";

  li.textContent = expression;
  li.appendChild(outputSpan);
  li.appendChild(arrow);
  li.appendChild(span);

  ul.appendChild(li);
  schemeContent.scrollTop = schemeContent.scrollHeight;
}

function displayResult(text, source, type) {
  const schemeContent = document.getElementById("scheme-content");
  const ul = schemeContent.querySelector("ul");
  const li = document.createElement("li");

  li.classList.add("icon", source, type);
  li.textContent = text;
  ul.appendChild(li);
  schemeContent.scrollTop = schemeContent.scrollHeight;
}

function evaluateScheme(expression) {
  try {
    if (window.Scheme && window.Scheme.eval) {
      return window.Scheme.eval(expression);
    } else {
      return "Scheme bridge not available.";
    }
  } catch (e) {
    return "Error: " + e.message;
  }
}

function generateSchemeButtons() {
  const buttonGrid = document.querySelector(".button-grid");

  buttonGrid.innerHTML = "";

  SCHEME_EXPRESSIONS.forEach(expression => {
    const localButton = document.createElement("button");

    localButton.className = "scheme-button";
    localButton.textContent = expression;
    localButton.setAttribute("data-rax", `click (eg ${expression})`);
    localButton.title = "Run locally";
    buttonGrid.appendChild(localButton);
  });

  const clearButton = document.createElement("button");

  clearButton.className = "scheme-button";
  clearButton.textContent = "🗑️ Clear Output";
  clearButton.setAttribute("data-rax", "click eg-clear");
  buttonGrid.appendChild(clearButton);
}

function runSchemeExpression(expression) {
  updateConnectionStatus("evaluating", "Evaluating: " + expression);
  displayExpressionWithSpans(" " + expression, "local", "local");

  const result = evaluateScheme(expression);

  setTimeout(() => {
    updateConnectionStatus("connected", "Ready.");
    completeEvaluation(expression, result, "local");
  }, 10);

  return result;
}

function updateConnectionStatus(statusType, message) {
  document.querySelector("#status-bar").className = statusType;
  document.querySelector("#connection-status-text").textContent = message;
}

function runSchemeScripts() {
  for (const s of document.querySelectorAll('script[type="application/x-scheme"]')) {
    if (s.hasAttribute("src")) {
      const src = s.getAttribute("src");
      const xhr = new XMLHttpRequest();

      xhr.open("GET", src, true);
      xhr.onload = function() {
        if (xhr.status === 200 || xhr.status === 0) {
          runSchemeExpression(xhr.responseText);
        } else {
          displayResult("Error loading " + src + ": HTTP " + xhr.status, "local", "error");
        }
      };
      xhr.onerror = function() {
        displayResult("Error loading " + src + ": Network error", "local", "error");
      };
      xhr.send();
    } else {
      runSchemeExpression(s.textContent);
    }
  }
}

function initializePage() {
  generateSchemeButtons();
  runSchemeScripts();
  installRAXHandlers(document.body);
}

window.addEventListener("load", initializePage);
