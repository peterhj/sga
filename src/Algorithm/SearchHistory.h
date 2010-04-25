//-----------------------------------------------
// Copyright 2009 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// SearchHistory - This class stores the sequence
// of discordant bases that were used while searching
// for a string in the FM-index
//
#ifndef SEARCHHISTORY_H
#define SEARCHHISTORY_H

#include "Util.h"

class SearchHistory
{
	struct HistoryItem
	{
		int pos;
		char base;

		friend std::ostream& operator<<(std::ostream& out, const HistoryItem& hi)
		{
			out << hi.pos << "," << hi.base;
			return out;
		}
	};
	typedef std::vector<HistoryItem> HistoryVector;


	public:
		
		//
		SearchHistory();

		//
		void add(int pos, char base);

		//
		friend std::ostream& operator<<(std::ostream& out, const SearchHistory& hist);

	private:

		HistoryVector m_history;
};

#endif
