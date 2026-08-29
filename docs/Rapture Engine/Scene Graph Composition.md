
I have not clue what this is called but take it as the paradigm of an engine. Godot uses a node based approach, Unreal their blueprint/actor way.

What is the case for Rapture then?
At the moment its like godot in a way, except that the hierarchy doesn't extend anything each scene object is its own thing, for extending one scene components are used, this way of thinking is more inline with unreal, except that unreal has this crap where they have a static mesh actor and a static mesh component.  
Not sure how godot does encapsulation of different things, for example take a moduler character, it has legs, arms torso and a head, these can either be 1 blueprint thingy like unreal would do with 4 mesh components, in godot (i believe) these would be 4 nodes probably parented under something. in Rapture same thing as godot, either parented to one of them or to a node3d. now placing thing "prefab" or scene object asset into a scene is just a blueprint (hihi) when thrown into the scene they are no longer assets, just part of the scene like any other thing, so calling them prefabs is kind of misleading, as there is no reference to where they came from, and would it even make sense? when we put this character in a scene its mutable, so we could add gloves as a child of the arms, what if we want to delete the character? what happens to the gloves? i have no idea what godot does here. in unreal it would be 1 thing, you would parent the gloves to the thing, to the 1 thing. which i think is clean and clear. 
ok checked and godot has the concept of "scenes" so even they put this prefab thing under 1 item in the outliner, i think the way they do "scenes" and its all just scenes inside of scenes is odd, i mean it makes sense and one way of looking at it its elegant, its all modular, but i dont know if i like it, its kind of what we do now but here its waaay worse than godot. so the easy way out would be to give these scene object assets/hierarchies, or prefabs as not a scene object but somethign else, and then we could add a scene object type (if we are following godot it would be scene/tscn : sceneobject), then they would be self contained. i hate that it makes soo much sense tho. fuuuuuck.

what do i not like about it?(godot's model)
 - idk, its kind of clean
 - whats irritating me??????
 - maybe its the fact that its a fake encapsulation/hierarchy? 
 - maybe the fact that i have this wrong idea of encapsulation? and that in reality some stuff should just be using a foldeR? like take a building, when a user is building it they want it maybe as 1 thing, a model something they can copy but also efficiently edit, if its this 1 thing you need to open another workspace to do that, i think my idea here is wrong, if its a finished house you dont, if its not ,and you are grayboxing just use a folder and have the option to multi select and make them into 1 thing, and make an asset out of it

overal its good, i have a hard time finding counter points, all thats left is the feeling, and id like to understand why before implementing it for real. so im not stuck with this feeling. 


also the fact that every asset uses rasset is probably terrible for version control, and readability
and for scripts , they most likely become an actual asset then a scripting workspace or something? again, dirty
ideally we have 1 script per prefab/tscn and one for the world maybe, the nice ui/ux is that in the level editor there is one a panel for a script that one is the one for the world, then in the scene object workspace there is 1 for the scene object, not per but for the tscn. then we wake up and realise we need modularity, shared files , like math helpers, how do we get these to be shared? we cant nicely, so its bolting on something else. if its instead actual lua file hierarchy we can just select which script this node uses. i guess what godot does for this is a script needs to mention "extends MeshInstance3D" altough our scirpting is more free, as you need to manually traverse to your owner. so our scripting is more fexible which is a downside here, as script.owner depends on the owner, so reusing a script is not flexible, its very dependent on the type. making them assets does auto solve duplication. oh then there is the editing part, do we edit scripts only in a dedicated scripting workspace? thats also something id ont like, but feels forced because of the modular scripts. 

so
1. whats this tscn feeling about?
2. what do i go for
3. do scripts become actual assets and i add some scripting workspace?
	1. note i hate that authoring a script then isnt nicely done in the scene object workspace.
4. should rasset be only used for runtime shiped games? no i guess in that case we would ship them even larger blobs, like 64mb blobs huh. no matter the choice we need some better way for version control, something in human readable format(not everything, or maybe make the metadata fully text so we version control can read something???).