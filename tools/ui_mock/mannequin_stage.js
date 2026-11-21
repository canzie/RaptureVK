// The mannequin a skeleton mock's viewport draws: the skinned body, its bones and joints, and the rotate ring on a joint

// the body the mesh draws: [from joint, to joint, thickness]
const MANNEQUIN_LIMBS = [
    ["pelvis", "spine_02", 58], ["spine_02", "neck", 50], ["neck", "head", 16],
    ["clavicle_l", "upperarm_l", 20], ["upperarm_l", "lowerarm_l", 20], ["lowerarm_l", "hand_l", 17],
    ["clavicle_r", "upperarm_r", 20], ["upperarm_r", "lowerarm_r", 20], ["lowerarm_r", "hand_r", 17],
    ["thigh_l", "calf_l", 27], ["calf_l", "foot_l", 22], ["foot_l", "ball_l", 15],
    ["thigh_r", "calf_r", 27], ["calf_r", "foot_r", 22], ["foot_r", "ball_r", 15],
];

/*
 * joints: MANNEQUIN_JOINTS or a subset; pose: 2 for the rest pose, 3 for the run pose
 * bodyOpacity: 0 leaves the mesh out; bones: draw the bones and joints; ring: the rotate gizmo on the selected joint
 * wireframe: wire lines over the body
 */
function mannequinStageSvg(joints, {selected = "", pose = 2, bodyOpacity = 1.0, bones = true, ring = false, wireframe = false,
    offset = [204, -30], scale = 1.45} = {}) {
    const byName = Object.fromEntries(joints.map(j => [j[0], j]));
    const toStage = ([x, y]) => [offset[0] + x * scale, offset[1] + y * scale];
    const at = name => toStage(byName[name][pose]);

    let body = "";
    for (const pass of [["#1A1B1D", 4], ["#8C929C", 0]]) {
        for (const [a, b, thickness] of MANNEQUIN_LIMBS) {
            const [p, q] = [at(a), at(b)];
            body += `<line x1="${p[0]}" y1="${p[1]}" x2="${q[0]}" y2="${q[1]}" stroke="${pass[0]}" stroke-width="${(thickness + pass[1]) * scale}" stroke-linecap="round"/>`;
        }
        const head = at("head");
        body += `<circle cx="${head[0]}" cy="${head[1] - 18 * scale}" r="${(26 + pass[1] / 2) * scale}" fill="${pass[0]}"/>`;
    }
    if (wireframe) {
        let wires = "";
        for (const [a, b, thickness] of MANNEQUIN_LIMBS) {
            const [p, q] = [at(a), at(b)];
            const length = Math.hypot(q[0] - p[0], q[1] - p[1]);
            const [nx, ny] = [-(q[1] - p[1]) / length, (q[0] - p[0]) / length];
            const half = thickness * scale / 2;
            for (const side of [-1, 0, 1]) {
                wires += `<line x1="${p[0] + nx * half * side}" y1="${p[1] + ny * half * side}" x2="${q[0] + nx * half * side}" y2="${q[1] + ny * half * side}"/>`;
            }
            for (let t = 0; t <= 1; t += 0.2) {
                const [cx, cy] = [p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t];
                wires += `<line x1="${cx - nx * half}" y1="${cy - ny * half}" x2="${cx + nx * half}" y2="${cy + ny * half}"/>`;
            }
        }
        body += `<g stroke="rgba(255,255,255,0.35)" stroke-width="1">${wires}</g>`;
    }

    let skeleton = "";
    if (bones) {
        for (const [name, parent] of joints) {
            if (parent === null) {
                continue;
            }
            const [p, q] = [at(parent), at(name)];
            const chosen = name === selected;
            skeleton += `<line x1="${p[0]}" y1="${p[1]}" x2="${q[0]}" y2="${q[1]}" stroke="${chosen ? "var(--accent)" : "#D9DCE1"}" stroke-width="${chosen ? 3 : 1.5}" opacity="0.9"/>`;
        }
        for (const [name] of joints) {
            const [x, y] = at(name);
            const chosen = name === selected;
            skeleton += `<circle cx="${x}" cy="${y}" r="${chosen ? 6 : 4}" fill="${chosen ? "var(--accent)" : "#1A1B1D"}" stroke="${chosen ? "#FFFFFF" : "#D9DCE1"}" stroke-width="1.5"/>`;
        }
    }

    let gizmo = "";
    if (ring && selected) {
        const [jx, jy] = at(selected);
        gizmo = `<g fill="none" stroke-width="2" opacity="0.95">
            <ellipse cx="${jx}" cy="${jy}" rx="54" ry="54" stroke="var(--axis-z)"/>
            <ellipse cx="${jx}" cy="${jy}" rx="54" ry="16" stroke="var(--axis-y)"/>
            <ellipse cx="${jx}" cy="${jy}" rx="16" ry="54" stroke="var(--axis-x)"/></g>`;
    }

    const [fx, fy] = toStage([200, 530]);
    const floor = `<ellipse cx="${fx}" cy="${fy + 6}" rx="${150 * scale}" ry="${26 * scale}" fill="rgba(0,0,0,0.3)"/>`;
    let grid = "";
    for (let i = -8; i <= 8; ++i) {
        grid += `<line x1="${fx + i * 70}" y1="${fy - 40}" x2="${fx + i * 150}" y2="${fy + 300}" stroke="#2E3034"/>`;
    }
    for (let r = 0; r < 6; ++r) {
        const y = fy - 40 + r * r * 12 + r * 14;
        grid += `<line x1="0" y1="${y}" x2="1400" y2="${y}" stroke="#2E3034"/>`;
    }
    return `<svg width="1000" height="900">${grid}${floor}<g opacity="${bodyOpacity}">${body}</g>${skeleton}${gizmo}</svg>`;
}
