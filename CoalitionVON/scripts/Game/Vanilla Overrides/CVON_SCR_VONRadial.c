[BaseContainerProps()]
modded class SCR_VONRadialDisplay : SCR_RadialMenuDisplay
{
	//This is how we switched from the 3D model to a png, so we don't need 800 niche radio models.
	//==========================================================================================================================================================================
	override void FadeItemPreview(bool state = true)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.FadeItemPreview(state);
			return;
		}
		if (state)
		{
			Widget radioIcon = GetRootWidget().FindAnyWidget("RadioIcon");
			radioIcon.SetOpacity(0);
			AnimateWidget.Opacity(radioIcon, 1, 1/0.3);
		}
	}
	
	override void SetPreviewItem(IEntity item)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.SetPreviewItem(item);
			return;
		}
		if (!item)
			return;
		
		if (!item.FindComponent(CVON_RadioComponent))
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent));
		ImageWidget radioIcon = ImageWidget.Cast(GetRootWidget().FindAnyWidget("RadioIcon"));
		radioIcon.LoadImageTexture(0, radioComp.m_sRadioIcon);
		radioIcon.SetImage(0);
	}
}