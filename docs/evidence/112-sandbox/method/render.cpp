// 中文用途：对全部112个恢复帧保存当前原图检测叠图；原图角点坐标保存在JSON，不把联系表当作精度真值。
#include <opencv2/opencv.hpp>
#include <fstream>
#include <map>
#include <set>
#include <filesystem>
int main(){cv::setNumThreads(1);std::ifstream ids("docs/evidence/112-forensics/deep/selected_ids.txt");std::set<int>wanted;int v;while(ids>>v)wanted.insert(v);cv::FileStorage def("docs/evidence/112-forensics/deep/sample_definition.json",cv::FileStorage::READ);wanted.clear();for(auto id:def["failed_ids"])wanted.insert((int)id);
 std::ifstream f("docs/evidence/112-sandbox/final-repeat2.jsonl");std::map<int,std::vector<cv::Point2f>>corners;std::string line;while(std::getline(f,line)){cv::FileStorage fs(line,cv::FileStorage::READ|cv::FileStorage::MEMORY|cv::FileStorage::FORMAT_JSON);int fid=fs["frame"];if(!wanted.count(fid))continue;for(auto d:fs["detections"])for(auto q:d){auto it=q.begin();float x=*it;++it;float y=*it;corners[fid].emplace_back(x,y);}}
 cv::VideoCapture cap("data/raw/marker_video.avi");cv::Mat img,contact(4*300,4*400,CV_8UC3,cv::Scalar(15,15,15));int fid=0,slot=0,sheet=0;
 while(cap.read(img)){if(corners.count(fid)){auto c=corners[fid];cv::Mat annotated=img.clone();std::vector<cv::Point> pts;for(auto q:c)pts.emplace_back(cvRound(q.x),cvRound(q.y));cv::polylines(annotated,pts,true,{0,255,0},1,cv::LINE_AA);for(int i=0;i<4;++i){cv::drawMarker(annotated,pts[i],{0,0,255},cv::MARKER_CROSS,9,1);cv::putText(annotated,"C"+std::to_string(i),pts[i]+cv::Point(4,-6),cv::FONT_HERSHEY_SIMPLEX,.4,{0,255,255},1);}
 cv::Rect r=cv::boundingRect(pts);r.x-=25;r.y-=25;r.width+=50;r.height+=50;r&=cv::Rect(0,0,img.cols,img.rows);cv::Mat crop=annotated(r);cv::imwrite("docs/evidence/112-sandbox/images/recovered-f"+std::to_string(fid)+".png",crop);cv::Mat resized,tile(300,400,CV_8UC3,cv::Scalar(15,15,15));double scale=std::min(380./crop.cols,265./crop.rows);cv::resize(crop,resized,{},scale,scale,cv::INTER_AREA);resized.copyTo(tile(cv::Rect((400-resized.cols)/2,30,resized.cols,resized.rows)));cv::putText(tile,"f"+std::to_string(fid),{10,23},cv::FONT_HERSHEY_SIMPLEX,.6,{0,255,255},1);tile.copyTo(contact(cv::Rect(slot%4*400,slot/4*300,400,300)));if(++slot==16){cv::imwrite("docs/evidence/112-sandbox/images/contact-"+std::to_string(++sheet)+".png",contact);contact.setTo(cv::Scalar(15,15,15));slot=0;}}++fid;}
 if(slot)cv::imwrite("docs/evidence/112-sandbox/images/contact-"+std::to_string(++sheet)+".png",contact);return corners.size()==112?0:2;
}
