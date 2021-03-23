#include "CLI/CLI.hpp"
#include "Channel.hpp"
#include "Event.hpp"
#include "TCanvas.h"
#include "TFile.h"
#include "TH1F.h"
#include "TROOT.h"
#include "TSpectrum.h"
#include "TTree.h"
#include "TTreeReader.h"
#include "TPaveLabel.h"
#include "TStyle.h"
#include "TLine.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include "fmt/color.h"


#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#elif defined(__linux__)
#include <sys/ioctl.h>
#endif // Windows/Linux

void get_terminal_size(int& width, int& height) {
  #if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
  width = (int)(csbi.dwSize.X);
  height = (int)(csbi.dwSize.Y);
  #elif defined(__linux__)
  struct winsize w;
  ioctl(fileno(stdout), TIOCGWINSZ, &w);
  width = (int)(w.ws_col);
  height = (int)(w.ws_row);
  #endif // Windows/Linux
}


int NbrEventToProcess(int& nbrEvents, const Long64_t& nentries)
{
  if(nbrEvents == 0) return nentries;
  else if(nbrEvents > nentries)
  {
    std::cout << "WARNING : You ask to process " << nbrEvents << " but this run only have " << nentries << " !!!";
    return nbrEvents = nentries;
  }
  else
    return nbrEvents;
}

class PositiveNegative
{
public:
  PositiveNegative() {}
  PositiveNegative(const std::string& PN) { Parse(PN); }
  bool        IsPositiveSignal() { return m_positive; }
  std::string asString()
  {
    if(m_positive) return "POSITIVE";
    else
      return "NEGATIVE";
  }

private:
  void Parse(const std::string& PN)
  {
    if(PN == "Positive" || PN == "POSITIVE" || PN == "P" || PN == "+") m_positive = true;
    else if(PN == "Negative" || PN == "NEGATIVE" || PN == "N" || PN == "-")
      m_positive = false;
    else
    {
      std::cout << "BAD argument !!!" << std::endl;
      std::exit(-5);
    }
  }
  bool m_positive{false};
};

class channel
{
public:
  channel(const int& chn, const std::string& PN): m_channelNumber(chn), m_PN(PN) {}
  channel(){};
  int         getChannelNumber() { return m_channelNumber; }
  bool        isPositive() { return m_PN.IsPositiveSignal(); }
  bool        isNegative() { return !m_PN.IsPositiveSignal(); }
  std::string PNasString() { return m_PN.asString(); }
  int getPolarity()
  {
    if(isPositive()) return 1;
    else return -1;
  }
private:
  int              m_channelNumber{-1};
  PositiveNegative m_PN;
};

class Channels
{
public:
  std::size_t getNumberOfChannelActivated() { return m_Channel.size(); }
  void        activateChannel(const int& chn, const std::string& PN) { m_Channel.emplace(chn, channel(chn, PN)); }
  void        print()
  {
    std::cout << "Channels ENABLED for the analysis : \n";
    for(std::map<int, channel>::iterator it = m_Channel.begin(); it != m_Channel.end(); ++it)
    { std::cout << "\t--> Channel " << it->first << " is declared to have " << it->second.PNasString() << " signal" << std::endl; }
  }
  bool DontAnalyseIt(const int& channel)
  {
    if(m_Channel.find(channel) == m_Channel.end()) return true;
    else
      return false;
  }
  bool ShouldBePositive(const int& ch) { return m_Channel[ch].isPositive(); }
  int getPolarity(const int& ch) { return m_Channel[ch].getPolarity(); }
private:
  std::map<int, channel> m_Channel;
};

TH1D CreateAndFillWaveform(const int& eventNbr, const Channel& channel, const std::string& name = "", const std::string title = "")
{
  std::string my_name  = name + " channel " + std::to_string(int(channel.Number));
  std::string my_title = title + " channel " + std::to_string(int(channel.Number));
  TH1D        th1(my_title.c_str(), my_name.c_str(), channel.Data.size(), 0, channel.Data.size());
  for(std::size_t i = 0; i != channel.Data.size(); ++i)
  {th1.Fill(i, channel.Data[i]);
  std::cout<<channel.Data[i]<<std::endl;}
//  auto                      result = std::minmax_element(channel.Data.begin(), channel.Data.end());
// std::pair<double, double> minmax((*result.first), (*result.second));
 // th1.GetYaxis()->SetRangeUser((minmax.first - ((minmax.second - minmax.first))*0.05 / 100.0), (minmax.second + ((minmax.second - minmax.first))*0.05 / 100.0));
  return std::move(th1);
}

void SupressBaseLine(Channel& channel)
{
  double min=9999.;
  double max=-9999.;
  double meanwindows{0};
  int bin{0};
  for(std::size_t j = 0; j != channel.Data.size(); ++j)
  {
      bin++;
      meanwindows += channel.Data[j];
  }
  meanwindows/=bin;
  for(std::size_t j = 0; j != channel.Data.size(); ++j)
  {
    channel.Data[j]-=meanwindows;
  }
}

double getAbsMax(Channel& channel)
{
  double max=-1.0;
  for(std::size_t j = 0; j != channel.Data.size(); ++j)
  {
    if(std::fabs(channel.Data[j])>max) max=std::fabs(channel.Data[j]);
  }
  return max;
}

void Normalise(Channel& channel,const double& max)
{
  for(std::size_t j = 0; j != channel.Data.size(); ++j)
  {
    channel.Data[j]=channel.Data[j]/max;
  }
}

std::pair<std::pair<double, double>, std::pair<double, double>> MeanSTD(const Channel& channel, const std::pair<double, double>& window_signal = std::pair<double, double>{99999999, -999999},
                                                                        const std::pair<double, double>& window_noise = std::pair<double, double>{99999999, -999999})
{
  double meanwindows{0};
  double sigmawindows{0};
  int    binusedwindows{0};
  double meannoise{0};
  double sigmanoise{0};
  int    binusednoise{0};
  for(std::size_t j = 0; j != channel.Data.size(); ++j)
  {
    ////////////////////////////////
    if(j >= window_noise.first && j <= window_noise.second)
    {
      binusednoise++;
      meannoise += channel.Data[j];
    }
    if(j >= window_signal.first && j <= window_signal.second)
    {
      binusedwindows++;
      meanwindows += channel.Data[j];
    }
  }
  meannoise /= binusednoise;
  meanwindows /= binusedwindows;
  for(std::size_t j = 0; j != channel.Data.size(); ++j)
  {
    ////////////////////////////////
    if(j >= window_noise.first && j <= window_noise.second)
    {
      sigmanoise += (channel.Data[j] - meannoise) * (channel.Data[j] - meannoise);
    }
    if(j >= window_signal.first && j <= window_signal.second)
    {
      sigmawindows += (channel.Data[j] - meanwindows) * (channel.Data[j] - meanwindows);
    }
  }
  sigmanoise   = std::sqrt(sigmanoise / (binusednoise-1));
  sigmawindows = std::sqrt(sigmawindows / (binusedwindows-1));


  std::pair<double, double> noise(meannoise, sigmanoise);
  std::pair<double, double> signal(meanwindows, sigmawindows);
  return std::pair<std::pair<double, double>, std::pair<double, double>>(noise, signal);
}

class Chamber
{
public:
  void   activateChannel(const int& chn, const std::string& PN) { m_Channels.activateChannel(chn, PN); }
  double getEfficiency() { return m_NumberFired * 1.0 / m_Total; }
  double getMultiplicity() { return m_TotalHit * 1.0 / m_NumberFired; }

private:
  int      m_TotalHit{0};
  int      m_NumberFired{0};
  int      m_Total{0};
  Channels m_Channels;
};

/*
 * TH1D CreateSelectionPlot(const TH1D& th)
 * {
 *    TH1D selected=th;
 *    selected.SetLineColor(kRed);
 *    std::string name="Selected_"+std::string(th.GetName());
 *    std::string title="Selected "+std::string(th.GetTitle());
 *    selected.SetTitle(title.c_str());
 *    selected.SetName(name.c_str());
 *    return std::move(selected);
 * }*/

//####FIX ME PUT THIS AS PARAMETER OUT OF THE PROGRAM ######

int main(int argc, char** argv)
{
  gStyle->SetOptStat(0);
  gErrorIgnoreLevel = kWarning;
  int width=0, height=0;
  CLI::App    app{"Analysis"};
  std::string file{""};
  app.add_option("-f,--file", file, "Name of the file to process")->required()->check(CLI::ExistingFile);
  int NbrEvents{0};
  app.add_option("-e,--events", NbrEvents, "Number of event to process", 0)->check(CLI::PositiveNumber);
  std::string nameTree{"Tree"};
  app.add_option("-t,--tree", nameTree, "Name of the TTree", "Tree");
  std::pair<double, double> SignalWindow;
  app.add_option("-s,--signal", SignalWindow, "Signal window")->required()->type_size(2);
  std::pair<double, double> NoiseWindow;
  app.add_option("-n,--noise", NoiseWindow, "Noise window")->required()->type_size(2);

  int NumberChambers{0};
  app.add_option("-c,--chambers", NumberChambers, "Number of chamber(s)")->check(CLI::PositiveNumber)->required();
  std::vector<int> distribution;
  app.add_option("-d,--distribution", distribution, "Channel is in wich chamber start at 0 and -1 if not connected")->required();
  std::vector<std::string> polarity;
  app.add_option("-p,--polarity", polarity, "Polarity of the signal Positive,+,Negative,-")->required();
  try
  {
    app.parse(argc, argv);
  }
  catch(const CLI::ParseError& e)
  {
    return app.exit(e);
  }
  //Open The file
  TFile fileIn(file.c_str());
  if(fileIn.IsZombie())
  {
    std::cout << "File Not Opened" << std::endl;
    std::exit(-3);
  }
  TTree* Run = static_cast<TTree*>(fileIn.Get(nameTree.c_str()));
  if(Run == nullptr || Run->IsZombie())
  {
    std::cout << "Problem Opening TTree \"Tree\" !!!" << std::endl;
    std::exit(-4);
  }

  TH1D     sigmas_event("Ration_event", "Ration_event", 100, 0, 5);
  TH1D     sigmas_noise("Ration_event", "Ration_event", 100, 0, 5);
  double   scalefactor = 1.0;
  Channels channels;
  channels.activateChannel(0, "N");
  channels.activateChannel(1, "N");
  channels.activateChannel(2, "N");
  channels.activateChannel(3, "N");
  channels.activateChannel(4, "N");
  channels.activateChannel(5, "N");
  channels.activateChannel(6, "N");
  channels.activateChannel(7, "N");
 // channels.activateChannel(8, "N");
 /* channels.activateChannel(9, "N");
  channels.activateChannel(10, "N");
  channels.activateChannel(11, "N");
  channels.activateChannel(12, "N");
  channels.activateChannel(13, "N");
  channels.activateChannel(14, "N");
  channels.activateChannel(15, "N");
  channels.activateChannel(16, "N");*/

  Long64_t NEntries = Run->GetEntries();
  NbrEvents         = NbrEventToProcess(NbrEvents, NEntries);
  channels.print();
  Event* event{nullptr};
  int    good_stack{0};
  bool   good = false;
  bool   hasseensomething{false};
  if(Run->SetBranchAddress("Events", &event))
  {
    std::cout << "Error while SetBranchAddress !!!" << std::endl;
    std::exit(-5);
  }


  // std::vector<TH1D> Verif;
  std::map<int, int> Efficiency;
  for(Long64_t evt = 0; evt < NbrEvents; ++evt)
  {
    get_terminal_size(width, height);
    fmt::print(fg(fmt::color::orange) | fmt::emphasis::bold,"┌{0:─^{2}}┐\n"
                                                            "│{1: ^{2}}│\n"
                                                            "└{0:─^{2}}┘\n"
                                                           ,"", fmt::format("Event {}",evt), width-2);


    TCanvas can(("Event "+std::to_string(evt)).c_str(),("Event "+std::to_string(evt)).c_str(),1280,720);
    TPaveLabel title(0.01, 0.965, 0.95, 0.99, ("Event "+std::to_string(evt)).c_str());
    title.Draw();
    TPad graphPad("Graphs","Graphs",0.01,0.01,0.995,0.96);
    graphPad.Draw();
    graphPad.cd();
    graphPad.Divide(1,8,0.,0.);

    event->clear();
    Run->GetEntry(evt);
    std::vector<TH1D> Plots(event->Channels.size());
    float min=+9999.0;
    float max=-9999.0;
    for(unsigned int ch = 0; ch != event->Channels.size(); ++ch)
    {
      if(channels.DontAnalyseIt(ch)) continue;  // Data for channel X is in file but i dont give a *** to analyse it !
      get_terminal_size(width, height);
      fmt::print(fg(fmt::color::white) | fmt::emphasis::bold,"┌{0:─^{2}}┐\n"
      "│{1: ^{2}}│\n"
      "└{0:─^{2}}┘\n"
      ,"", fmt::format("Channel {}",ch), width-2);
      if(evt == 0) Efficiency[ch] = 0;
      SupressBaseLine(event->Channels[ch]);
      double max=getAbsMax(event->Channels[ch]);
      Normalise(event->Channels[ch],max);
      Plots[ch]=CreateAndFillWaveform(evt, event->Channels[ch], "Waveform", "Waveform");
      std::pair<std::pair<double, double>, std::pair<double, double>> meanstd  = MeanSTD(event->Channels[ch], SignalWindow, NoiseWindow);



      // std::cout<<"Event "<<evt<<" Channel "<<ch<<"/n";
      // std::cout<<" Mean : "<<meanstd.first<<" STD :
      // "<<meanstd.second<<std::endl;
      // selected with be updated each time we make some selection... For now
      // it's the same as Waveform one but in Red !!!
      // TH1D selected = CreateSelectionPlot(waveform);

      if((meanstd.second.first-meanstd.first.first)*channels.getPolarity(ch) < 2 * meanstd.first.second) hasseensomething = false;
      else hasseensomething = true;


      if(hasseensomething == true)
      {
        good = true;
        //hasseensomething=true;
        Plots[ch].SetLineColor(4);
        //waveform.Scale(1.0 / 4096);

        // if(channels.ShouldBePositive(ch)) waveform.Fit(f1);
        // else waveform.Fit(f1);

        //can.SaveAs(("GOOD/GOOD" + std::to_string(evt) + "_Channel" + std::to_string(ch) + ".pdf").c_str(),"Q");
        fmt::print(fg(fmt::color::green) | fmt::emphasis::bold,"{:^{}}\n",fmt::format("Signal region : {:03.2f}+-{:03.2f} Noise region : {:03.2f}+-{:03.2f}",meanstd.second.first,meanstd.second.second,meanstd.first.first,meanstd.first.second),width);
      }
      else
      {
        Plots[ch].SetLineColor(16);
        // waveform.Scale(1.0 / 4096);
        // if(channels.ShouldBePositive(ch)) waveform.Fit(f1);
        // else waveform.Fit(f1);
        //can.SaveAs(("BAD/BAD" + std::to_string(evt) + "_Channel" + std::to_string(ch) + ".pdf").c_str(),"Q");
        fmt::print(fg(fmt::color::red) | fmt::emphasis::bold,"{:^{}}\n",fmt::format("Signal region : {:03.2f}+-{:03.2f} Noise region : {:03.2f}+-{:03.2f}",meanstd.second.first,meanstd.second.second,meanstd.first.first,meanstd.first.second),width);
      }
      graphPad.cd(ch+1);
      gStyle->SetLineWidth(gStyle->GetLineWidth() / 4);
      Plots[ch].GetXaxis()->SetRangeUser(0, 1024);
      Plots[ch].GetYaxis()->SetNdivisions(10,0,0);
      Plots[ch].GetXaxis()->SetNdivisions(10,10,0);
      Plots[ch].GetYaxis()->SetRangeUser(-1.2,1.2);
      Plots[ch].GetYaxis()->SetLabelSize(0.07);
      Plots[ch].SetStats();
      Plots[ch].SetTitle(";");
      if(ch==7)
      {


      Plots[ch].GetXaxis()->SetLabelOffset(0.02);
      Plots[ch].GetXaxis()->SetLabelSize(0.1);


      }
      else
      {
        Plots[ch].GetXaxis()->SetTitleOffset(0.);
        Plots[ch].GetXaxis()->SetLabelSize(0.);
        Plots[ch].GetXaxis()->SetTitleSize(0.);


      }
      Plots[ch].Draw("HIST");
      TLine event_min;
      event_min.SetLineColor(15);
      event_min.SetLineWidth(1);
      event_min.SetLineStyle(2);
      event_min.DrawLine(SignalWindow.first,-200,SignalWindow.first,200);
      event_min.DrawLine(SignalWindow.second,-200,SignalWindow.second,200);
      event_min.SetLineColor(16);
      event_min.SetLineWidth(1);
      event_min.SetLineStyle(4);
      event_min.DrawLine(NoiseWindow.first,-200,NoiseWindow.first,200);
      event_min.DrawLine(NoiseWindow.second,-200,NoiseWindow.second,200);


      //Mean noise
      event_min.SetLineColor(46);
      event_min.DrawLine(NoiseWindow.first,meanstd.first.first,NoiseWindow.second,meanstd.first.first);

      //Mean Signal
      event_min.SetLineColor(40);
      event_min.DrawLine(SignalWindow.first,meanstd.second.first,SignalWindow.second,meanstd.second.first);

      //Bars
      event_min.SetLineColor(kBlack);
      event_min.SetLineStyle(4);
      std::cout<<meanstd.first.first-2 * meanstd.first.second<<" "<<meanstd.first.first+2 * meanstd.first.second<<std::endl;
      event_min.DrawLine(0,meanstd.first.first-2 * meanstd.first.second,1024,meanstd.first.first-2 * meanstd.first.second);
      event_min.DrawLine(0,meanstd.first.first+2 * meanstd.first.second,1024,meanstd.first.first+2 * meanstd.first.second);

      /*
      event_min.SetLineColor(kRed);
      event_min.DrawLine(SignalWindow.first,(meanstd.second.first-meanstd.first.first)*channels.getPolarity(ch),SignalWindow.second,(meanstd.second.first-meanstd.first.first)*channels.getPolarity(ch));

      event_min.SetLineColor(kGreen);
      event_min.DrawLine(0,meanstd.first.first+2 * meanstd.first.second,1024,meanstd.first.first+2 * meanstd.first.second);
      event_min.DrawLine(0,meanstd.first.first-2 * meanstd.first.second,1024,meanstd.first.first-2 * meanstd.first.second);
      event_min.Draw();*/
    }
    can.SetTitle(("Event " + std::to_string(evt)).c_str());
    can.SetName(("Event " + std::to_string(evt)).c_str());
    can.SaveAs(("Event " + std::to_string(evt) + ".pdf").c_str(),"Q");

    if(good == true)
    {
      //hasseensomething=true;
      good_stack++;
      good = false;
    }
  }
  float efficiency=good_stack * 1.00 / (NbrEvents * scalefactor);
  std::cout << "Chamber efficiency " << efficiency << " +-" <<std::sqrt(efficiency*(1-efficiency)/NbrEvents)<< std::endl;
  if(event != nullptr) delete event;
  if(Run != nullptr) delete Run;
  if(fileIn.IsOpen()) fileIn.Close();
  std::cout << "BYE !!!" << std::endl;
}
