
### Components

- WorldEnvironementComponent
    - Controls stuff like wind


### dependencies
trace shader
    - needs to wait for(3 pipeline barriers):
        - raydata texture needs to be ready from previous frame (mostly just an extra)
        - prev textures need to be finished drawing

    - will transition (1 pipeline barrier):
        - raydata texture to shader read

(flatten shader)
    - needs to wait for (1 pipeline barrier):
        - trace shader to finish
    - will transition (1 pipeline barrier):
        - flatten texture to shader read

blending shaders(4):
    - needs to wait for (1 pipeline barrier):
        - raydata to be in shader mode
        - prev texture needs to be in shader mode
        - current texture needs to be in image storage mode
    - will transition (1 pipeline barrier):
        - blending texture to shader read






### J*B SYSTEM
#### Requirements

- easy to add new jobs
- easy to set dependencies between jobs
- a way to cancel jobs and its children
- a way to wait for a job to finish
- lightweight
- job queues can be static (dependency chain needs to be defined when the job queue is created)


TODO
- general descriptor manager
- fix the gizmo rotation math
- completly redo the project setup structure to consitently build every lib the same way

--------------------------------

- ray picking

- skybox

- ddgi

- static meshes

- materials in the asset manager

- physics
- animations
- giga serializaiton
- terrain
- post processing
- procedural stuff
- some limit testing
- audio
- ui
- game?


