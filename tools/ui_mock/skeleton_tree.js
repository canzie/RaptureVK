// The Skeleton panel the skeleton, skeletal mesh and animation workspaces share: the joint hierarchy, read only unless editable

// SK_Mannequin: [name, parent, rest position, run position] in viewport units, the run pose is Run_Forward at frame 14
const MANNEQUIN_JOINTS = [
    ["root", null, [200, 530], [200, 530]],
    ["pelvis", "root", [200, 300], [204, 312]],
    ["spine_01", "pelvis", [200, 258], [208, 272]],
    ["spine_02", "spine_01", [200, 218], [213, 233]],
    ["neck", "spine_02", [200, 160], [222, 176]],
    ["head", "neck", [200, 124], [228, 140]],
    ["clavicle_l", "spine_02", [222, 172], [236, 186]],
    ["upperarm_l", "clavicle_l", [250, 176], [256, 190]],
    ["lowerarm_l", "upperarm_l", [284, 238], [238, 248]],
    ["hand_l", "lowerarm_l", [310, 296], [262, 286]],
    ["clavicle_r", "spine_02", [178, 172], [196, 184]],
    ["upperarm_r", "clavicle_r", [150, 176], [176, 188]],
    ["lowerarm_r", "upperarm_r", [116, 238], [200, 236]],
    ["hand_r", "lowerarm_r", [90, 296], [238, 210]],
    ["thigh_l", "pelvis", [222, 306], [224, 318]],
    ["calf_l", "thigh_l", [230, 410], [262, 398]],
    ["foot_l", "calf_l", [234, 508], [236, 496]],
    ["ball_l", "foot_l", [254, 526], [262, 508]],
    ["thigh_r", "pelvis", [178, 306], [186, 318]],
    ["calf_r", "thigh_r", [170, 410], [158, 404]],
    ["foot_r", "calf_r", [166, 508], [112, 450]],
    ["ball_r", "foot_r", [146, 526], [96, 470]],
];

const SKELETON_TREE_ROW = 24;
const SKELETON_TREE_TOP = 40;

/*
 * joints: [name, parent, ...]; selected: a joint name
 * editable: joints can be added, renamed, reparented and deleted
 * trail(name): text at the row's end, or "" for none
 * marked(name): the row carries a diamond, a key in an animation
 * menuOn: a joint name whose right click menu is open
 */
function skeletonTreeBody(joints, {selected = "", editable = false, trail = () => "", marked = () => false, menuOn = ""} = {}) {
    const byName = Object.fromEntries(joints.map(j => [j[0], j]));
    const depthOf = name => byName[name][1] === null ? 0 : depthOf(byName[name][1]) + 1;
    const rows = joints.map(([name], r) => {
        const depth = depthOf(name);
        const hasChildren = joints.some(j => j[1] === name);
        const guides = Array.from({length: depth}, (_, d) => `<span class="guide" style="left:${15 + d * 14}px"></span>`).join("");
        const expander = icon(hasChildren ? "CHEVRON_DOWN" : "CARET_RIGHT", `expander ${hasChildren ? "" : "leaf"}`);
        const trailText = trail(name);
        return `<div class="tree-row joint-row ${name === selected ? "selected" : ""}" style="top:${SKELETON_TREE_TOP + r * SKELETON_TREE_ROW}px">${guides}
            <span style="width:${8 + depth * 14}px;flex:none"></span>${expander}${icon("BONE", "tree-icon")}<span>${name}</span>
            ${marked(name) ? '<span class="keyed"></span>' : ""}${trailText ? `<span class="joint-trail">${trailText}</span>` : ""}
            <span style="width:12px;flex:none"></span></div>`;
    }).join("");
    const lock = editable ? "" : `<span class="icon-button read-only-lock" style="right:36px;top:9px">${icon("LOCK")}</span>`;
    const menu = menuOn ? skeletonTreeMenu(joints.findIndex(j => j[0] === menuOn), editable) : "";
    return `<div class="search" style="left:8px;right:${editable ? 40 : 68}px;top:8px">${icon("SEARCH")}Search joints</div>${lock}
        <span class="icon-button" style="right:8px;top:9px">${icon("FILTER")}</span>${rows}
        <div class="tree-fill" style="top:${SKELETON_TREE_TOP + joints.length * SKELETON_TREE_ROW}px"></div>${menu}`;
}

function skeletonTreeMenu(row, editable) {
    const top = SKELETON_TREE_TOP + row * SKELETON_TREE_ROW + 18;
    const editing = editable ? `<div class="separator"></div><div class="item">${icon("PLUS")}Add child joint</div><div class="item">Rename</div>
        <div class="item danger">Delete</div>` : "";
    return `${mouseCursor([150, top - 6])}<div class="menu" style="left:156px;top:${top}px;z-index:15">
        <div class="item">${icon("VIEWPORT")}Frame in viewport</div><div class="item">${icon("COPY")}Copy name</div>
        <div class="item">Select children</div>${editing}</div>`;
}
