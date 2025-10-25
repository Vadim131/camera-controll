TH1I* build_1dimhist(TString filename)
{
    TFile* f = TFile::Open(filename);
    TH2I* hist2d = (TH2I*)f->Get("hist2d");
    int* data = hist2d->GetArray();

    TH1I* hist1d = new TH1I("hist1d", "hist1d", 65536, 0, 65536);

    for (int i = 0; i < hist2d->GetEntries(); i++)
        hist1d->Fill(data[i]);

    //hist1d->Draw();
    return hist1d;
}

std::pair<std::vector<double>, std::vector<double>> calc_stat(const std::vector<std::vector<int>> array)
{
	std::vector<double> means;
	means.resize(3388*2712, 0);
	std::vector<double> devs;
	devs.resize(3388*2712, 0);

	for (const auto& vec: array)
	{
            for (int i = 0; i < vec.size(); i++)
	    {
	        means.at(i) += vec.at(i) * 1./ array.size();
	    }
	}

	for (const auto& vec: array)
	{
            for (int i = 0; i < vec.size(); i++)
	    {
	        devs.at(i) += pow((means.at(i) - vec.at(i)) / array.size(), 2);
	    }
	}

	for (int i = 0; i < 3388*2712; i++)
	    devs.at(i) = sqrt(devs.at(i));

	return {means, devs};
}

std::vector<int> fill_data(TString fname, double& time)
{
    std::vector<int> res;
    // Image size 3388 x 2712
    res.reserve(3388*2712);
    TFile* f = TFile::Open(fname);
    TH2I* hist2d = (TH2I*)f->Get("hist2d");
    int* data = hist2d->GetArray();

    for (int i = 0; i < hist2d->GetEntries(); i++)
        res.push_back(data[i]);

    time = ((TParameter<double>*)f->Get("exposureTime"))->GetVal();

    f->Close();
    return res;
}

void fill_tree(const char* dirname, const char* ext)
{
    TSystemDirectory dir(dirname, dirname);
    TList *files = dir.GetListOfFiles();
    if (files) {
        TSystemFile *file;
        TString fname;
        TIter next(files);
	std::map<int, std::vector<std::vector<int>>> array;
	double time;
        while ((file=(TSystemFile*)next())) {
            fname = TString(dirname) + "/" + file->GetName();
            if (!file->IsDirectory() && fname.EndsWith(ext)) {
	            std::cout << fname << std::endl;
		    auto data = fill_data(fname, time);
		    array[time*1e3].push_back(data);
            }
        }

	TFile *res_file = new TFile("res.root", "UPDATE");
	TTree* tree = new TTree("tree", "tree");
	int col, row;
	double mean, dev;
	tree->Branch("col", &col);
	tree->Branch("row", &row);
	tree->Branch("mean", &mean);
	tree->Branch("dev", &dev);
	tree->Branch("time", &time);
	for (auto it = array.begin(); it != array.end(); ++it)
	{
	    time = it->first * 1.e-3;
	    auto [means, devs] = calc_stat(it->second);
	    std::cout << means[0] << " " << devs[0] << " " << time << std::endl;
	    for (int i = 0; i < 3388; i++)
	    {
	        for (int j = 0; j < 2712; j++)
	        {
		        row = j;
		        col = i;
		        mean = means.at(3388*j + i);
		        dev = devs.at(3388*j + i);
		        tree->Fill();
	        }
	    }
	}
	tree->Write();
    }
}
