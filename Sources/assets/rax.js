const EVENT_PROPERTIES = {
  "animationend": ["animationName", "elapsedTime"],
  "animationiteration": ["animationName", "elapsedTime"],
  "animationstart": ["animationName", "elapsedTime"],
  "beforeunload": [],
  "blur": ["relatedTarget"],
  "change": ["value"],
  "click": ["altKey", "button", "clientX", "clientY", "ctrlKey", "metaKey", "shiftKey"],
  "contextmenu": ["button", "clientX", "clientY"],
  "copy": ["clipboardData"],
  "cut": ["clipboardData"],
  "dblclick": ["altKey", "button", "clientX", "clientY", "ctrlKey", "metaKey", "shiftKey"],
  "drag": ["clientX", "clientY", "dataTransfer"],
  "dragend": ["clientX", "clientY", "dataTransfer"],
  "dragenter": ["clientX", "clientY", "dataTransfer"],
  "dragleave": ["clientX", "clientY", "dataTransfer"],
  "dragover": ["clientX", "clientY", "dataTransfer"],
  "dragstart": ["clientX", "clientY", "dataTransfer"],
  "drop": ["clientX", "clientY", "dataTransfer"],
  "ended": [],
  "focus": ["relatedTarget"],
  "focusin": ["relatedTarget"],
  "focusout": ["relatedTarget"],
  "input": ["data", "inputType"],
  "keydown": ["altKey", "code", "ctrlKey", "key", "keyCode", "metaKey", "repeat", "shiftKey"],
  "keypress": ["altKey", "code", "ctrlKey", "key", "keyCode", "metaKey", "shiftKey"],
  "keyup": ["altKey", "code", "ctrlKey", "key", "keyCode", "metaKey", "shiftKey"],
  "load": [],
  "mousedown": ["altKey", "button", "clientX", "clientY", "ctrlKey", "metaKey", "shiftKey"],
  "mouseenter": ["clientX", "clientY"],
  "mouseleave": ["clientX", "clientY"],
  "mousemove": ["altKey", "clientX", "clientY", "ctrlKey", "metaKey", "movementX", "movementY", "shiftKey"],
  "mouseout": ["clientX", "clientY", "relatedTarget"],
  "mouseover": ["clientX", "clientY", "relatedTarget"],
  "mouseup": ["altKey", "button", "clientX", "clientY", "ctrlKey", "metaKey", "shiftKey"],
  "paste": ["clipboardData"],
  "pause": [],
  "play": [],
  "reset": [],
  "resize": ["innerHeight", "innerWidth"],
  "scroll": ["scrollX", "scrollY"],
  "select": [],
  "submit": [],
  "timeupdate": ["currentTime"],
  "touchcancel": ["changedTouches", "targetTouches", "touches"],
  "touchend": ["changedTouches", "targetTouches", "touches"],
  "touchmove": ["changedTouches", "targetTouches", "touches"],
  "touchstart": ["changedTouches", "targetTouches", "touches"],
  "transitionend": ["elapsedTime", "propertyName"],
  "unload": [],
  "volumechange": ["muted", "volume"],
  "wheel": ["deltaMode", "deltaX", "deltaY", "deltaZ"]
};

function eventToJSON(event) {
  const result = {};

  for (const property of EVENT_PROPERTIES[event.type] || []) {
    result[property] = event[property];
  }
  return JSON.stringify(result);
}

function formToJSON(form) {
  const object = {};

  for (const [key, value] of new FormData(form).entries()) {
    object[key] = object[key] ? [...object[key], value] : [value];
  }
  return JSON.stringify(object);
}

function querySelectorAllInclusive(root, selector) {
  return [...(root.matches(selector) ? [root] : []),
          ...root.querySelectorAll(selector)];
}

function escapeForScheme(string) {
  return string.replace(/\\/g, "\\\\")
    .replace(/"/g, "\\\"")
    .replace(/\n/g, "\\n")
    .replace(/\r/g, "\\r")
    .replace(/\t/g, "\\t");
}

function readAndExecute(expression) {
  let enabled = true;

  return function(event) {
    if (enabled) {
      enabled = false;
      runSchemeExpression(
        `(${expression} "${escapeForScheme(eventToJSON(event))}")`);
      enabled = true;
    }
  };
}

function submitAndExecute(element) {
  return function(event) {
    if (document.querySelectorAll(".rax-executing").length == 0) {
      event.preventDefault();
      event.stopPropagation();
      event.target.classList.add("rax-executing");

      runSchemeExpression(
        `(${element["action"]} "${escapeForScheme(formToJSON(element))}")`);
    }
  };
}

// Parse "<event-type> <scheme-expression>", returning a two-element array.
function parseSpec(attribute) {
  return attribute.match(/^(\S+)\s(.+)$/).slice(1);
}

// Return every attribute of element whose name starts with "data-rax", in
// alphabetical order.
function raxAttributes(element) {
  return [...element.attributes].filter(a => a.name.startsWith("data-rax"))
    .sort((a1, a2) => a1.name < a2.name ? -1 : a1.name === a2.name ? 0 : 1);
}

function* straySpecs() {
  for (const element of document.querySelectorAll("[data-rax]")) {
    const attribute = element.getAttribute("data-rax");
    const [type, expression] = parseSpec(attribute);

    if (type == "stray") {
      yield expression;
    }
  }
}

function handleStrayClicks(event) {
  const target = event.target;
  const tag = target.tagName;

  if (tag != "BUTTON"
      && tag != "INPUT"
      && tag != "SELECT"
      && tag != "TEXTAREA"
      && !target.getAttribute("data-rax")) {
    window.scrollTo(0, 0);

    for (const expression of straySpecs()) {
      readAndExecute(expression)(event);
    }
  }
}

function installStrayHandler() {
  document.body.addEventListener("click", handleStrayClicks);
}

function installRAXHandlers(root) {
  for (const element of querySelectorAllInclusive(root, "[data-rax]")) {
    for (const a of raxAttributes(element)) {
      const [type, expression] = parseSpec(a.value);

      console.log("type = " + type + ", expression = " + expression); // <><>

      if (type == "input") {
        element.addEventListener(
          type,
          readAndExecute(expression));
      } else if (type == "submit") {
        element.addEventListener("submit", submitAndExecute(element));
      } else if (type == "stray") {
        installStrayHandler();
      } else {
        // <> Handle each variant of <UIEvent> specially.  For example, for
        // keyboard events, provide a way to specify what key maps to what
        // action.
        element.addEventListener(type, readAndExecute(expression));
      }
    }
  }
}