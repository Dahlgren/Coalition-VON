[BaseContainerProps()]
class CVON_GroupFrequencyContainer
{
	[Attribute()] ref array<string> m_aGroupNames;
	[Attribute()] ref array<string> m_aSRFrequencies;
	[Attribute()] ref array<string> m_aLRFrequencies;
}

[BaseContainerProps()]
modded class SCR_FactionCallsignInfo
{
	[Attribute()] ref array<ref CVON_GroupFrequencyContainer> m_aGroupFrequencyOverrides;
}