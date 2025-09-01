[BaseContainerProps()]
class CVON_GroupFrequencyContainer
{
	[Attribute()] ref array<string> m_aGroupNames;
	[Attribute()] ref array<string> m_aSRFrequencies;
	[Attribute()] ref array<string> m_aLRFrequencies;
	[Attribute()] ref array<string> m_aMRFrequencies;
}

[BaseContainerProps()]
modded class SCR_FactionCallsignInfo
{
	[Attribute()] ref array<ref CVON_GroupFrequencyContainer> m_aGroupFrequencyOverrides;
}