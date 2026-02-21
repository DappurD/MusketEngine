shader_type spatial;
render_mode cull_back;

// Team palette
uniform vec3 team0_color : source_color = vec3(0.15, 0.22, 0.55); // France: dark blue
uniform vec3 team1_color : source_color = vec3(0.6, 0.12, 0.12); // Russia: crimson red
uniform vec3 neutral_color : source_color = vec3(0.6, 0.55, 0.45); // Buff trim

varying float v_team;

void vertex() {
	v_team = INSTANCE_CUSTOM.g;
}

void fragment() {
	vec3 base = mix(team0_color, team1_color, clamp(v_team, 0.0, 1.0));

	// Subtle variation from vertex position
	float variation = fract(sin(dot(VERTEX.xz, vec2(12.9898, 78.233))) * 43758.5453);
	base = mix(base, neutral_color, variation * 0.15);

	ALBEDO = base;
	ROUGHNESS = 0.75;
	METALLIC = 0.0;
}
