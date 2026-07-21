# Agent Workspace Rules

## File Generation Constraints

- **No C-Drive/Default Artifact Storage**: Do not save generated images, documents, reports, diagram markdown files, or other artifacts in the default C-drive appdata directory (`C:\Users\云宝\.gemini\antigravity\brain\...`).
- **Workspace-Local Storage**: 
  - Save all generated images in the folder `<workspace_root>/images/` (e.g. `./images/`).
  - Save all documents, diagrams, and reports in the folder `<workspace_root>/docs/` (e.g. `./docs/`).
- **Path Resolution**: Resolve all paths relative to the current workspace root folder, ensuring that files are placed locally within the project.
