"""Run with Unreal's PythonScript commandlet after building the project module."""
import unreal

asset_path = "/Game/YOPO/Blueprints/BP_YOPOGoalSender"
parent_class = unreal.load_class(None, "/Script/yopo_rally_ue.YOPOGoalSender")
if parent_class is None:
    raise RuntimeError("Build and load the project module before creating the blueprint")

if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(asset_path)
    if generated_class is None or not unreal.get_default_object(generated_class).is_a(parent_class):
        raise RuntimeError("An unrelated asset already exists at " + asset_path)
else:
    unreal.EditorAssetLibrary.make_directory("/Game/YOPO/Blueprints")
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_YOPOGoalSender", "/Game/YOPO/Blueprints", unreal.Blueprint, factory
    )
    if blueprint is None:
        raise RuntimeError("Failed to create " + asset_path)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint):
        raise RuntimeError("Failed to save " + asset_path)

unreal.log("YOPO_GOAL_BLUEPRINT_READY: " + asset_path)
