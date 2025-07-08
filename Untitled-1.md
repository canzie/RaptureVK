
# Roblox Developement


## AI environement setup

Use cursor.ai, copilot, windsurf, claudeCode or geminiCLI
- https://cursor.com/home

use the free version to start, for the pro version use your student mail here https://cursor.com/students

In cursor go to file>preferences>cursor settings>
 - models: enable claude-4-sonnet, gemini-2.5-pro and o3
 - rules: you can enable rules, i will provide these in the github repo, make sure it is enabled

You can use the ai for code, planning, bug fixing, ideas, etc.

### AI models
- **claude-4-sonnet** is good for coding larger chunks (uses most requests)
- **gemini-2.5-pro** is good for planning, coding, best all rounder
- **o3** is insane at reasoning, finding bugs, understanding complex problems, shit at coding



 ## Github and Git

how git works:
you have the remote version of the repo, hosted on github and a local version. changes you make to the local version ONLY are permanent and accesible to other people when you push them to the remote version. For commit messages dont write garbage, mention what you did and changed in the commit. best practice is to commit often and small. for developement we each work either on a seperate branch or a branch per feature-ish. when a feature is finished and stable, merge with main or some other stable branch. DO NOT FORGET TO PUSH ATLEAST ONCE PER DAY. you can go back to previous commits, so think of them as checkpoints if you fuck up

simple commands 
- **git clone** [link to repo.git]
- **git pull** (changes local version of the repo)
- **git fetch** (same thing but doesn't working directoty)
- **git add .** (adds all changes in your local version for staging)
- **git commit -m "message"** (commits the changes to the local version)
- **git push** (pushes the changes to the remote version of the current repo in the current branch)
- **git push origin [branch name]** (pushes the changes to the remote version of the current repo in the specified branch)
- **git checkout [branch name]** (switches to the specified branch)
- **git checkout -b [branch name]** (creates a new branch and switches to it)
- **git branch** (lists all branches)
- **git branch -d [branch name]** (deletes the specified branch)
- **git merge [branch name]** (merges the specified branch into the current branch, e.g. you made changes on branch1 and now you want to update main, then go to main and do 'git merge branch1')


## Roblox Studio integration

we use rojo, so install the vscode extension and the roblox studio plugin. here are the raw links: vscode/cursor extension https://marketplace.visualstudio.com/items?itemName=evaera.vscode-rojo roblox plugin: https://create.roblox.com/store/asset/6415005344/Rojo-7

then install the luau extension for robloxes lua language support

I forgor the rest 💀, ask the ai to help you with the rest

**try it out on your own project to test, ask the ai to do simple things like add a block or setup the project with rojo. Whenever you do something or ask it to do something, ask it to explain what it did and why, this will help you learn and understand roblox studio better**







