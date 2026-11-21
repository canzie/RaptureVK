// The window chrome every workspace mock shares: menu bar, workspace tabs, hotbar, docked panels, bottom bar and a property sheet
const params = new URLSearchParams(location.search);

// Asset_colorForType, Editor/src/ui/AssetVisuals.cpp
const ASSET_COLORS = {
    texture: "rgb(235, 102, 199)", material: "rgb(173, 115, 242)", mesh: "rgb(242, 153, 64)", module: "rgb(128, 128, 242)",
    animation: "rgb(242, 209, 77)", world: "rgb(184, 217, 77)", script: "rgb(89, 191, 217)", skeleton: "rgb(140, 140, 140)",
};

const icon = (name, cls = "", style = "") => `<span class="icon ${cls}" style="${style}">${ICONS[name] || ""}</span>`;

function mouseCursor([x, y]) {
    return `<svg class="abs" style="left:${x}px;top:${y}px;z-index:20" width="14" height="20" viewBox="0 0 14 20">
        <path d="M1 1 L1 16 L5 12 L8 19 L10 18 L7 11 L12 11 Z" fill="#FFFFFF" stroke="#000000" stroke-width="1"/></svg>`;
}

// tabs: [iconName, assetKind, label]; the last one is in front
function windowChrome(tabs, hotbarGroups) {
    const tabHtml = tabs.map(([iconName, kind, label], i) => `<div class="workspace-tab ${i === tabs.length - 1 ? "active" : ""}">
        ${icon(iconName, "kind", `color:${ASSET_COLORS[kind] || "var(--text-2)"}`)}<span class="ellipsis">${label}</span>${icon("X", "close")}</div>`).join("");
    return `<div id="menubar" class="abs">${["File", "Edit", "Window", "Help"].map(t => `<div class="item">${t}</div>`).join("")}
            <div id="project" class="abs">${icon("FOLDER")}dwaqjdoiawd</div></div>
        <div id="workspace-tabs" class="abs">${tabHtml}</div>
        <div id="hotbar" class="abs">${hotbarGroups.join('<div class="island-separator"></div>')}<div class="island-separator"></div></div>`;
}

function hotbarGroup(html, cls = "") {
    return `<div class="hotbar-group ${cls}">${html}</div>`;
}

function statusNote(text, color = "var(--status-success)") {
    return hotbarGroup(`<span class="dot" style="background:${color}"></span>${text}`, "status-note");
}

// tabs: [{icon, label}], the first one in front
function panel(id, box, tabs, body) {
    const tabHtml = tabs.map((t, i) => `<div class="tab ${i === 0 ? "active" : ""}" style="left:${i * 162}px">
        ${t.icon ? icon(t.icon) : ""}<span class="ellipsis">${t.label}</span>${i === 0 ? icon("X", "close") : ""}</div>`).join("");
    return `<div id="${id}" class="panel" style="left:${box[0]}px;top:${box[1]}px;width:${box[2]}px;height:${box[3]}px">
        <div class="tabs">${tabHtml}</div><div class="body">${body}</div></div>`;
}

function bottomBar() {
    return `<div id="bottom-bar" class="abs"><div class="section browser">Content Browser</div><div class="island-separator"></div>
        <div class="section command-section"><div class="command">Command</div></div><div class="island-separator"></div>
        <div class="section stats-gap"></div><div class="island-separator"></div>
        <div class="section stats"><span><b>8.3</b> ms</span><span><b>120</b> fps</span><span><b>412</b> MB VRAM</span></div></div>`;
}

// the property sheet the Properties and Details panels share: collapsible headers over label and field rows
class PropertySheet {
    constructor(top = 10) {
        this.top = top;
        this.html = "";
    }

    header(title, iconName, more = true, tag = "") {
        this.html += `<div class="component-header" style="top:${this.top}px">${icon("CHEVRON_DOWN", "chevron")}${icon(iconName)}${title}
            ${tag ? `<span class="tag">${tag}</span>` : ""}
            ${more ? `<span class="icon-button">${icon("MORE")}</span>` : ""}</div>`;
        this.top += 28;
        return this;
    }

    row(label, fields, {changed = false, height = 30, cls = ""} = {}) {
        const reset = changed ? `<span class="reset">${icon("RESET")}</span>` : "";
        this.html += `<div class="property-row ${changed ? "changed" : ""} ${cls}" style="top:${this.top}px;height:${height}px"><div class="label">${label}</div>
            <div class="divider"></div><div class="fields">${fields}</div>${reset}</div>`;
        this.top += height;
        return this;
    }

    gap(height) {
        this.top += height;
        return this;
    }
}

// the X, Y and Z ball gizmo in a viewport's top right corner
function orientationGizmo() {
    const center = [60, 60];
    const axes = [["X", [36, -14], "--axis-x"], ["Y", [0, -40], "--axis-y"], ["Z", [26, 24], "--axis-z"]];
    let negatives = "", positives = "";
    for (const [label, [dx, dy], color] of axes) {
        negatives += `<circle cx="${center[0] - dx}" cy="${center[1] - dy}" r="7" fill="#1E1E1E" stroke="var(${color})" stroke-width="2"/>`;
        positives += `<line x1="${center[0]}" y1="${center[1]}" x2="${center[0] + dx}" y2="${center[1] + dy}" stroke="var(${color})" stroke-width="2"/>
            <circle cx="${center[0] + dx}" cy="${center[1] + dy}" r="9" fill="var(${color})"/>
            <text x="${center[0] + dx}" y="${center[1] + dy + 4}" text-anchor="middle" font-size="11" font-weight="700" fill="#141414">${label}</text>`;
    }
    return `<svg class="gizmo" width="120" height="120" viewBox="0 0 120 120">${negatives}${positives}</svg>`;
}

const AXES = ["--axis-x", "--axis-y", "--axis-z", "--text-3"];
const vectorFields = values => values.map((v, i) => `<div class="field number"><span class="axis" style="background:var(${AXES[i]})"></span>${v}</div>`).join("");
const textField = text => `<div class="field text single">${text}</div>`;
const readOnly = text => `<span class="read-only">${text}</span>`;
const dropdown = text => `<div class="field text single">${text}${icon("CHEVRON_DOWN", "caret")}</div>`;
const slider = (text, fraction) => `<div class="field text single"><div class="fill" style="width:${fraction * 100}%"></div><span class="value">${text}</span></div>`;
const checkbox = on => `<div class="checkbox">${on ? icon("CHECK") : ""}</div>`;
const assetPicker = (thumb, name, kind) => `<div class="asset-field"><div class="thumb" style="background:${thumb}"></div>
    <div><div class="name">${name}</div><div class="kind">${kind}</div></div>
    <div class="actions"><span class="nav-button">${icon("SEARCH")}</span><span class="nav-button">${icon("X")}</span></div></div>`;
