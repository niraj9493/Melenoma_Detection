// Melanoma_Detection.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include "opencv2/objdetect.hpp"
#include "opencv2/ml.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include <opencv2/core/core.hpp>

using namespace cv;
using namespace std;
class Melanoma_Detection {

public:

	static void Denoise(Mat* img)
	{
		Mat temp;
		GaussianBlur(*img, *img, Size(7, 7), 0, 0, BORDER_CONSTANT);
	}

	static void EqualizeHistogram(Mat* img, vector<Mat>* color_channels)
	{
		Ptr<CLAHE> clahe = createCLAHE();
		clahe->setClipLimit(3);
		color_channels->clear();
		cvtColor(*img, *img, COLOR_BGR2YCrCb);	//Can't equilize a BRG, have to convert it first
		split(*img, *color_channels);						//Split the color channels
		clahe->apply((*color_channels)[0], (*color_channels)[0]);
		//equalizeHist((*color_channels)[0], (*color_channels)[0]);	//Equilize the Y component, this will not effect color intensity
		merge(*color_channels, *img);
		cvtColor(*img, *img, COLOR_YCrCb2BGR);

	}

	/**Threshold each color channel to create a binary mask**/
	static void OtsuSplit(Mat* img, vector<Mat>* color_channels, Mat* dst)
	{
		color_channels->clear();
		split(*img, *color_channels);
		for (int i = 0; i < color_channels->size(); i++)
		{
			threshold((*color_channels)[i], (*color_channels)[i], 0, 255, CV_THRESH_BINARY_INV | CV_THRESH_OTSU);
		}

		BinaryMaskMajority(color_channels, dst);
	}

	static void BinaryMaskMajority(vector<Mat>* mask_channels, Mat* dst)
	{
		add((*mask_channels)[0], (*mask_channels)[1], *dst, Mat(), CV_32S);
		add(*dst, (*mask_channels)[2], *dst, Mat(), CV_32S);
		*dst = *dst / 3;
		dst->convertTo(*dst, CV_8U);
		threshold(*dst, *dst, 100 , 255, CV_THRESH_BINARY);

	}

		/** Denoise the image and equilize the histogram to
		account for varying lighting conditions**/
	static void Preprocessing(Mat* img, vector<Mat>* color_channels)
	{
		//Denoising the Image
		//Denoise(img);
		//Equilizing the histogram to deal with different lighting
		EqualizeHistogram(img, color_channels);
	}

	static void ApplyMask(Mat* img, Mat* mask)
	{
		Mat* temp = new Mat();
		img->copyTo(*temp, *mask);
		*img = *temp;
	}

	/** Split the image into various forms to prepare for feature
	extraction**/
	static void Segmentation(Mat* img, vector<Mat>* color_channels, Mat* dst)
	{
		OtsuSplit(img, color_channels, dst);
	}

	static void FeatureExtraction(Mat* img, Mat* mask, vector<float>* dst, HOGDescriptor* hog) 
	{
		//vector<vector<Point>> contours;
		morphologyEx(*mask, *mask, MORPH_OPEN, getStructuringElement(MORPH_RECT, Size(6, 6)));

		medianBlur(*mask, *mask, 7);
		medianBlur(*mask, *mask, 5);
		medianBlur(*mask, *mask, 3);

		morphologyEx(*mask, *mask, MORPH_OPEN | MORPH_CLOSE, getStructuringElement(MORPH_RECT, Size(4, 4)));

		//findContours(*mask, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
		ApplyMask(img, mask);
		//drawContours(*mask, contours, -1, Scalar(0, 256, 0), 2);
		hog->compute(*mask, *dst);
	}

static void ProcessImages(vector<Mat>* imgs, vector<Mat>* training_data, HOGDescriptor* descriptor)
{
	Mat prep_img;
	Mat binary_mask;
	vector<float> hog;
	vector<Mat> color_channels;

	for (int i = 0; i < imgs->size(); i++)
	{

		(*imgs)[i].copyTo(prep_img);

		Melanoma_Detection::Preprocessing(&prep_img, &color_channels);

		Melanoma_Detection::Segmentation(&prep_img, &color_channels, &binary_mask);

		Melanoma_Detection::FeatureExtraction(&(*imgs)[i], &binary_mask, &hog, descriptor);



		training_data->push_back(Mat(hog).clone());


		cout << "HOG for image " << i << "extracted" << endl;
	}
}


static void BatchTrain(string pos_path, string neg_path, Ptr<ml::SVM>& svm, Ptr<ml::SVM> svm_auto, HOGDescriptor* hog)
{
	vector<cv::String> img_paths;
	vector<cv::String> neg_paths;
	vector<Mat> img_list;
	vector<Mat>  neg_list;
	vector<Mat> training_samples;
	Mat training_data;
	vector<int> labels;
	Mat img;

	size_t positive_count;
	size_t negative_count;

	glob(pos_path, img_paths, false);
	cout << img_paths.size()<<endl;
	for (int i = 0; i < img_paths.size(); i++)
	{
		img = imread(img_paths[i]);
		if (img.empty()) // Check for failure
		{
			cout << "Could not open or find the image: " << img_paths[i] << endl;
			system("pause"); //wait for any key press
			exit(-1);
		}
		resize(img, img, Size(600, 480));
		img_list.push_back(img);
		cout << i <<endl;
	}

	ProcessImages(&img_list, &training_samples, hog);


	positive_count = training_samples.size();
	labels.assign(positive_count, +1);

	img_paths.clear();
	glob(neg_path, neg_paths, false);

	for (int i = 0; i < neg_paths.size(); i++)
	{
		img = imread(neg_paths[i]);
		if (img.empty()) // Check for failure
		{
			cout << "Could not open or find the image: " << img_paths[i] << endl;
			system("pause"); //wait for any key press
			exit(-1);
		}
		resize(img, img, Size(600, 480));
		neg_list.push_back(img);
		cout << i << endl;
	}

	ProcessImages(&neg_list, &training_samples, hog);

	negative_count = training_samples.size() - positive_count;
	labels.insert(labels.end(), negative_count, -1);
	CV_Assert(positive_count < labels.size());

	cout << "Converting Begin" << endl;

	ConvertForSVM(&training_samples, &training_data);

	cout << "Converting End" << endl;

	svm->setDegree(3);
	svm->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER + TermCriteria::EPS, 1000, 1e-3));
	svm->setKernel(ml::SVM::RBF);
	svm->setC(1000); // From paper, soft classifier
	svm->setType(ml::SVM::C_SVC); // C_SVC; // EPSILON_SVR; // may be also NU_SVR; // do regression task

	cout << "SVM Training Begin"<<endl;

	svm->train(training_data, ml::ROW_SAMPLE, labels);

	cout << "SVM Training End" << endl;

	cout<<"Saving SVM"<<endl;
	svm->save("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/RBF/Melanoma_SVM.xml");
	cout << "SVM Save Complete" << endl;


	svm_auto->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER + TermCriteria::EPS, 1000, 1e-3));
	svm_auto->setKernel(ml::SVM::RBF);
	svm_auto->setType(ml::SVM::C_SVC); // C_SVC; // EPSILON_SVR; // may be also NU_SVR; // do regression task

	cout << "SVM Training Auto Begin" << endl;

	svm_auto->trainAuto(training_data, ml::ROW_SAMPLE, labels);

	cout << "SVM Training Auto End" << endl;

	cout << "Saving SVM Auto" << endl;
	svm_auto->save("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/RBF/Melanoma_SVM_Auto.xml");
	cout << "SVM Auto Save Complete" << endl;

}

static vector< float > GetSVMDetector(const Ptr< ml::SVM >& svm)
{
	// get the support vectors
	Mat sv = svm->getSupportVectors();
	const int sv_total = sv.rows;
	// get the decision function
	Mat alpha, svidx;
	double rho = svm->getDecisionFunction(0, alpha, svidx);

	CV_Assert(alpha.total() == 1 && svidx.total() == 1 && sv_total == 1);
	CV_Assert((alpha.type() == CV_64F && alpha.at<double>(0) == 1.) ||
		(alpha.type() == CV_32F && alpha.at<float>(0) == 1.f));
	CV_Assert(sv.type() == CV_32F);

	vector< float > hog_detector(sv.cols + 1);
	memcpy(&hog_detector[0], sv.ptr(), sv.cols * sizeof(hog_detector[0]));
	hog_detector[sv.cols] = (float)-rho;
	return hog_detector;
}

static void ConvertForSVM(vector<Mat>* training_samples, Mat* training_data)
{
	int rows = (int)training_samples->size();
	int cols = (int)std::max((*training_samples)[0].cols, (*training_samples)[0].rows);
	Mat ff;
	*training_data = Mat(rows, cols, CV_32FC1);

	for (size_t i = 0; i < (*training_samples).size(); i++)
	{
		CV_Assert((*training_samples)[i].cols == 1 || (*training_samples)[i].rows == 1);
		cout << "Converting Sample " << i << endl;
		if((*training_samples)[i].cols == 1)
		{
			transpose((*training_samples)[i], ff);
			ff.copyTo(training_data->row((int)i));
		}
		else if ((*training_samples)[i].rows == 1)
		{
			(*training_samples)[i].copyTo(training_data->row((int)i));
		}
	}
}

static void Classify(Mat* img, HOGDescriptor* hog, Ptr<ml::SVM> svm, float* result)
{
	Mat prep_img;
	Mat binary_mask;
	Mat img_with_mask;
	vector<Mat> color_channels;
	vector<float> hog_gr;

	cout << "Classifying..." << endl;

	resize(*img, *img, Size(600, 480));

	img->copyTo(prep_img);

	Melanoma_Detection::Preprocessing(&prep_img, &color_channels);

	Melanoma_Detection::Segmentation(&prep_img, &color_channels, &binary_mask);

	Melanoma_Detection::FeatureExtraction(img, &binary_mask, &hog_gr, hog);

	Mat temp;
	transpose(Mat(hog_gr), temp);

	*result = svm->predict(temp);
}

static void Test(string positive_path, string negative_path, HOGDescriptor* hog, Ptr<ml::SVM> svm, float* accuracy)
{
	vector<cv::String> pos_img_paths;
	vector<cv::String> neg_img_paths;

	glob(positive_path, pos_img_paths, false);
	glob(negative_path, neg_img_paths, false);
	vector<float> results;
	Mat img;

	float temp;
	string o;

	cout << "Positive Testing Set Size: " << pos_img_paths.size() << endl;
	for (int i = 0; i < pos_img_paths.size(); i++)
	{
		img = imread(pos_img_paths[i]);
		if (img.empty()) // Check for failure
		{
			cout << "Could not open or find the image: " << pos_img_paths[i] << endl;
			system("pause"); //wait for any key press
			exit(-1);
		}
		resize(img, img, Size(600, 480));
		Melanoma_Detection::Classify(&img, hog, svm, &temp);
		o = "Correct!";
		if (temp < 0)
		{
			temp = 0;
			o = "Wrong!";
		}
		cout << o << endl;
		results.push_back(temp);
		cout << i << endl;
	}

	cout << "Negative Testing Set Size: " << neg_img_paths.size() << endl;
	for (int i = 0; i < neg_img_paths.size(); i++)
	{
		img = imread(neg_img_paths[i]);
		if (img.empty()) // Check for failure
		{
			cout << "Could not open or find the image: " << neg_img_paths[i] << endl;
			system("pause"); //wait for any key press
			exit(-1);
		}
		resize(img, img, Size(600, 480));
		Melanoma_Detection::Classify(&img, hog, svm, &temp);
		o = "Correct!";
		if (temp > 0)
		{
			temp = 0;
			o = "Wrong!";
		}
		cout << o << endl;
		results.push_back(-temp);
		cout << i << endl;
	}

	*accuracy = cv::sum(results)[0] / results.size();


}
};

int main(int argc, char** argv)
{
	string input;
	bool running = true;
	
	Ptr <ml::SVM> svm;
	Ptr <ml::SVM> svm_auto;

	Ptr <ml::SVM> svm_load;
	Ptr <ml::SVM> svm_load_auto;

	Ptr <ml::SVM> svm_linear;
	Ptr <ml::SVM> svm_linear_auto;

	HOGDescriptor hog(Size(600, 480), Size(16, 16), Size(8, 8), Size(8, 8), 9, 4, 0.2, true, HOGDescriptor::DEFAULT_NLEVELS, 1);

		cout << "Loading SVM Linear Auto..." << endl;
		svm = ml::SVM::load("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/Linear/Melanoma_SVM.xml");
		cout << "SVM Linear Auto Loaded" << endl;

		cout << "Loading SVM RBF Auto..." << endl;
		svm_auto = ml::SVM::load("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/RBF/Melanoma_SVM_Auto.xml");
		cout << "SVM RBF Auto Loaded" << endl;

	while (running)
	{
		cout << "Please select Train, Test, Classify or Exit" << endl;
		cin >> input;
		transform(input.begin(), input.end(), input.begin(), tolower);

		if (input == "train")
		{

			svm = ml::SVM::create();
			svm_auto = ml::SVM::create();

			cout << "Training..." << endl;

			string positive_path = "C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma Inception/data/positive/*.jpg";
			string negative_path = "C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma Inception/data/negative/*.jpg";

			Melanoma_Detection::BatchTrain(positive_path, negative_path, svm, svm_auto, &hog);

			cout << "Training Complete" << endl;
		}
		else if (input == "test")
		{
			if (svm_linear == NULL) 
			{
				cout << "Loading Linear SVM..." << endl;
				svm_linear = ml::SVM::load("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/Linear/Melanoma_SVM.xml");
				cout << "Linear SVM Sucessfully loaded" << endl;
			}

			if (svm_linear_auto == NULL) 
			{
				cout << "Loading Linear Auto-Trained SVM..." << endl;
				svm_linear_auto = ml::SVM::load("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/Linear/Melanoma_SVM_Auto.xml");
				cout << "Linear Auto-Trained SVM Sucessfully Loaded" << endl;
			}

			if (svm_load == NULL)
			{
				cout << "Loading RBF SVM..." << endl;
				svm_load = ml::SVM::load("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/RBF/Melanoma_SVM.xml");
				cout << "Linear SVM Sucessfully loaded" << endl;
			}

			if (svm_load_auto == NULL)
			{
				cout << "Loading RBF Auto-Trained SVM..." << endl;
				svm_load_auto = ml::SVM::load("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/Models/RBF/Melanoma_SVM_Auto.xml");
				cout << "Linear Auto-Trained SVM Sucessfully Loaded" << endl;
			}



			string positive_path = "C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma Inception/test/data_positive/*.jpg";
			string negative_path = "C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma Inception/test/data_negative/*.jpg";

			float accuracy_linear;
			float accuracy_linear_auto;
			float accuracy_RBF_load;
			float accuracy_RBF_load_auto;

			cout << "Testing Linear SVM..." << endl;
			Melanoma_Detection::Test(positive_path, negative_path, &hog, svm_linear, &accuracy_linear);
			cout << "Linear SVM Testing Complete" << endl;

			cout << "Testing Linear Auto-Trained SVM..." << endl;
			Melanoma_Detection::Test(positive_path, negative_path, &hog, svm_linear_auto, &accuracy_linear_auto);
			cout << "Linear Auto-Trained SVM Testing Complete" << endl;

			cout << "Testing Loaded RBF SVM..." << endl;
			Melanoma_Detection::Test(positive_path, negative_path, &hog, svm_load, &accuracy_RBF_load);
			cout << "Loaded RBF SVM Testing Complete" << endl;

			cout << "Testing Loaded RBF Auto-Trained SVM..." << endl;
			Melanoma_Detection::Test(positive_path, negative_path, &hog, svm_load_auto, &accuracy_RBF_load_auto);
			cout << "Loaded RBF Auto-Trained SVM Testing Complete" << endl;


			cout << "Linear SVM Accuracy: " << 100*accuracy_linear <<"%"<< endl;
			cout << "Linear SVM Auto Accuracy: " << 100*accuracy_linear_auto <<"%"<< endl;

			cout << "Loaaded RBF SVM Accuracy: " << 100 * accuracy_RBF_load << "%" << endl;
			cout << "Loaded RBF SVM Auto Accuracy: " << 100 * accuracy_RBF_load_auto << "%" << endl;

		}
		else if (input == "classify")
		{
			string path;
			bool valid_path = false;
			Mat img;


			img = imread("C:/Users/Travis/Desktop/School/SJSU/Machine Learning/Melanoma SVM/ISIC_0000013.jpg");

			float result;
			float result_auto;

			Melanoma_Detection::Classify(&img, &hog, svm, &result);
			Melanoma_Detection::Classify(&img, &hog, svm_auto, &result_auto);

			cout << "Classification Complete" << endl;

			cout << "The Auto-Trained SVM result is: " << result << endl;
			cout << "The Auto-Trained RBF result is: " << result_auto << endl;

			cin.ignore();
		}
		else if (input == "exit")
		{
			cout << "exiting...";
			running = false;
		}


	}

	//String windowName = "Melanoma Detection"; //Name of the window

	//namedWindow(windowName); // Create a window

	//destroyWindow(windowName); //destroy the created window

	return 0;
}

