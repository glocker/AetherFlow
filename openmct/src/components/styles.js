// Use adoptedStyleSheets if browser supports
// Else fallback for <style>
export function applyComponentStyles(shadowRoot, cssText) {
  if (
    "adoptedStyleSheets" in Document.prototype &&
    "replaceSync" in CSSStyleSheet.prototype
  ) {
    const sheet = new CSSStyleSheet();
    sheet.replaceSync(cssText);
    shadowRoot.adoptedStyleSheets = [...shadowRoot.adoptedStyleSheets, sheet];
    return;
  }

  const style = document.createElement("style");
  style.textContent = cssText;
  shadowRoot.prepend(style);
}
