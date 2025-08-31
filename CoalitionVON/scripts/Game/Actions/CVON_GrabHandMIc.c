//SHhhhhh you never saw this

class CVON_GrabHandMic: SCR_ScriptedUserAction
{
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		SCR_PlayerController.Cast(GetGame().GetPlayerController()).OpenHandMicMenuClient(SCR_PlayerController.GetLocalPlayerId(), RplComponent.Cast(pOwnerEntity.FindComponent(RplComponent)).Id());
	}
	
	override bool CanBeShownScript(IEntity user)
	{
		return false;
	}
	
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}
}