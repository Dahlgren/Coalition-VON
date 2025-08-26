//This stores the container, not only to populate the channels for a side, but for the group that lines up with the same index.
//So the first group created gets the first GroupFrequencyContainer assigned to them for all their radios
//Auto assigning freqs based on this.
[BaseContainerProps()]
class CVON_GroupFrequencyContainer
{
	[Attribute()] string m_sSRFrequency;
	[Attribute()] string m_sLRFrequency;
	[Attribute()] string m_sMRFrequency;
}

[BaseContainerProps()]
modded class SCR_FactionCallsignInfo
{
	[Attribute()] ref array<ref CVON_GroupFrequencyContainer> m_aGroupFrequency;
}