shader_type canvas_item;

uniform float pulse = 0.0;

void fragment() {
    vec4 base = texture(TEXTURE, UV);
    COLOR = vec4(base.rgb + vec3(pulse * 0.01), base.a);
}
