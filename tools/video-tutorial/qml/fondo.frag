#version 440
// Fondo generativo del video (T017 D22; 04-motion-composicion.md, seccion 3):
// fBm de ruido simplex (5 octavas, lacunaridad 2, ganancia 0.45) con domain
// warping de dos niveles, gradiente de 4 paradas de la paleta, acento dorado
// en una franja estrecha (<= 5 % del area) y vineta radial (-20 % en bordes).
// Determinista: depende solo de los uniformes (tiempo derivado del contador
// de cuadros, semilla fija, fase por capitulo). Se compila a .qsb con qsb.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float tiempo;   // segundos de video * velocidad
    float semilla;
    float fase;     // desfase por capitulo (interpolado al cambiar)
    float aspecto;  // ancho / alto
    float giro;     // radianes (2 grados por minuto)
    float atenuacion; // T024.4: 0.6 bajo la marca de portada y cierre, 1.0 el resto
    vec4 c0;        // verde noche (base)
    vec4 c1;        // verde Santos
    vec4 c2;        // verde claro (altas luces)
    vec4 c3;        // casi negro (sombras)
    vec4 acento;    // dorado
};

// Ruido simplex 2D (Ashima Arts / Stefan Gustavson, dominio publico/MIT).
vec3 permutar(vec3 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }

float simplex(vec2 v)
{
    const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permutar(permutar(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float fbm(vec2 p)
{
    float suma = 0.0;
    float amplitud = 0.5;
    for (int i = 0; i < 5; ++i) {
        // Octavas altas atenuadas (sin grano fino que vibre con la compresion).
        suma += amplitud * simplex(p) * (i < 3 ? 1.0 : 0.5);
        p *= 2.0;
        amplitud *= 0.45;
    }
    return suma; // ~[-0.9, 0.9]
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 p = (uv - 0.5) * vec2(aspecto, 1.0) * 0.8; // escala amplia: sin grano fino
    float c = cos(giro);
    float s = sin(giro);
    p = mat2(c, -s, s, c) * p;
    // Semilla fija -> desplazamiento del dominio.
    p += vec2(fract(semilla * 0.6180339887) * 97.0, fract(semilla * 0.4142135623) * 89.0);
    p += vec2(fase, fase * 0.6);

    // Domain warping de dos niveles: f = fbm(p + fbm(p + fbm(p))).
    vec2 q = vec2(fbm(p + vec2(0.0, tiempo)), fbm(p + vec2(5.2, 1.3) - vec2(tiempo * 0.7, 0.0)));
    vec2 r = vec2(fbm(p + 1.0 * q + vec2(1.7, 9.2) + tiempo * 0.15),
                  fbm(p + 1.0 * q + vec2(8.3, 2.8) - tiempo * 0.12));
    float f = clamp(fbm(p + 1.0 * r) * 0.75 + 0.5, 0.0, 1.0);

    // Gradiente de 4 paradas: sombra -> base -> Santos -> luz.
    vec3 color = mix(c3.rgb, c0.rgb, smoothstep(0.0, 0.40, f));
    color = mix(color, c1.rgb, smoothstep(0.40, 0.70, f));
    color = mix(color, c2.rgb, smoothstep(0.75, 0.98, f) * 0.6);
    // Acento dorado: franja estrecha de la segunda capa del warp (<= 5 %).
    float franja = smoothstep(0.035, 0.0, abs(length(r) - 0.62)) * smoothstep(0.55, 0.75, f);
    color = mix(color, acento.rgb, franja * 0.55);
    // Atenuacion (portada y cierre), antes de la vineta.
    color *= atenuacion;
    // Vineta: oscurece los bordes un 20 %.
    float d = length((uv - 0.5) * vec2(aspecto, 1.0)) / (0.5 * length(vec2(aspecto, 1.0)));
    color *= 1.0 - 0.20 * smoothstep(0.35, 1.0, d);
    fragColor = vec4(color, 1.0) * qt_Opacity;
}
