// ParticipatingMedia.glsl - light travelling through a medium that absorbs and scatters it: phase functions,
// transmittance and the integration of in-scattered light over a march step

#ifndef PARTICIPATING_MEDIA_GLSL
#define PARTICIPATING_MEDIA_GLSL

const float MEDIA_PI = 3.14159265359;

// The phase functions take _cosTheta between the direction towards the light and the view direction

float mediaRayleighPhase(float _cosTheta) {
    return 3.0 / (16.0 * MEDIA_PI) * (1.0 + _cosTheta * _cosTheta);
}

float mediaCornetteShanksPhase(float _g, float _cosTheta) {
    float g2 = _g * _g;
    float k = 3.0 / (8.0 * MEDIA_PI) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + _cosTheta * _cosTheta) / pow(max(1.0 + g2 - 2.0 * _g * _cosTheta, 1e-6), 1.5);
}

float mediaUniformPhase() {
    return 1.0 / (4.0 * MEDIA_PI);
}

// Beer-Lambert: the fraction of light surviving _distance through a constant _extinction
vec3 mediaTransmittance(vec3 _extinction, float _distance) {
    return exp(-_extinction * _distance);
}

// The integral of transmittance over a step, from its start to where _stepTransmittance was reached.
// Multiplied by a constant in-scattered source it gives the light the step adds, exact for constant extinction
// where source * dt would overshoot (Hillaire 2015, Physically Based and Unified Volumetric Rendering in Frostbite, slide 28)
vec3 mediaStepIntegral(vec3 _extinction, vec3 _stepTransmittance) {
    return (vec3(1.0) - _stepTransmittance) / max(_extinction, vec3(1e-7));
}

#endif // PARTICIPATING_MEDIA_GLSL
