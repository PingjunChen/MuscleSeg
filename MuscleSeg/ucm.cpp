﻿/******************************************************************************
Copyright:  BICI2
Created:    20:4:2016 19:41
Filename:   ucm.cpp
Author:     Pingjun Chen

Purpose:    UCM implementation
******************************************************************************/


#include "ucm.h"
#include <omp.h>

namespace bici2
{
    UCM::UCM()
    {
        // std::cout << "Hello UCM" << std::endl;
    }

    UCM::UCM(std::string path)
    {
        this->imgpath_ = path;
        this->img_ = cv::imread(imgpath_);
    }

    UCM::UCM(const cv::Mat& ucm_input)
    {
        // this->img_ = ucm_input;
        ucm_input.copyTo(this->img_);
    }

    void UCM::SetPath(std::string path)
    {
        this->imgpath_ = path;
        this->img_ = cv::imread(imgpath_);
    }


    void UCM::SetMat(const cv::Mat& ucm_input)
    {
        // this->img_ = ucm_input;
        ucm_input.copyTo(this->img_);
    }

    std::string UCM::GetPath() const
    {
        return this->imgpath_;
    }

    cv::Mat UCM::GetMat() const
    {
        return this->img_;
    }

    // Save cv::Mat to YML file
    void UCM::SaveToYML(std::string yml_path) const
    {
        cv::FileStorage yml_fs(yml_path, cv::FileStorage::WRITE);
        yml_fs << "ucm_output" << this->img_;
        yml_fs.release();
    }

    UCM::~UCM()
    {
        // std::cout << "Byebye UCM" << std::endl;
    }

    // get the maximum and minimum value of Mat
    void UCM::MaxMinMat()
    {
        double min_val = 0.0;
        double max_val = 0.0;

        // get min and max value
        cv::minMaxIdx(img_, &min_val, &max_val);
        std::cout << "max value of mat " << min_val << std::endl;
        std::cout << "min value of mat " << max_val << std::endl;
    }

    // TODO(Pingjun Chen 22:4:2016): Not finished yet!
    cv::Mat UCM::ApplyUCM()
    {
        // applying LM filters
        cv::Mat filtered_imgs = ApplyLMFilter();
        // Contours to UCM
        cv::Mat ucm = Contour2UCM(filtered_imgs);
        // TODO(Pingjun Chen 22:4:2016): need to normalize cv::Mat ucm before return

        return ucm;
    }

    cv::Mat UCM::ApplyLMFilter()
    {
        // make LM filters
        int num_orientation = 8;
        bici2::MakeLMFilters make_lmfilter(num_orientation);
        cv::Mat lm_filters = make_lmfilter.GenerateLMfilters();

        // define filter to get individual filters from lm_filters
        int filter_rows = lm_filters.size[1];
        int filter_cols = lm_filters.size[2];
        cv::Mat filter = cv::Mat(filter_rows, filter_cols, CV_32F);


        // clone image and convert to float 
        cv::Mat input_img = this->img_.clone();
        if (CV_32F != input_img.type())
        {
            input_img.convertTo(input_img, CV_32F);
        }

        // applying filters
        int num_filter_used = 8;
        int starting_filter_index = 24;
        int img_rows = input_img.size[0];
        int img_cols = input_img.size[1];
        int filtered_img_size[3] = { num_filter_used, img_rows, img_cols };
        cv::Mat imgs_orient = cv::Mat(3, filtered_img_size, CV_32F);

        // define structuring element
        cv::Size ele_size = { 5, 5 };
        cv::Mat structure_ele = cv::getStructuringElement(cv::MORPH_ELLIPSE, ele_size);
        structure_ele.at<char>(1, 0) = 0;  // make changes to keep the structure
        structure_ele.at<char>(1, 4) = 0;  // element the same as one generated by 
        structure_ele.at<char>(3, 0) = 0;  // strel in Matlab
        structure_ele.at<char>(3, 4) = 0;

        cv::Mat filtered_img = cv::Mat(img_rows, img_cols, CV_32F);
        for (int ifilter = 0; ifilter < num_filter_used; ++ifilter)
        {
            // copy filter
            std::memcpy(filter.data, lm_filters.data +
                (starting_filter_index + ifilter)*filter_rows*filter_cols*lm_filters.elemSize(),
                filter_rows*filter_cols*lm_filters.elemSize());
            // apply filter

            // !!! filter2D and imfilter have different padding manner,
            // there will be difference in edge part.
            cv::filter2D(input_img, filtered_img, -1, filter);
            // add original image
            cv::add(input_img, filtered_img, filtered_img);
            // erode using structure element ('disk', 2)
            cv::erode(filtered_img, filtered_img, structure_ele);

            // store filtered image
            std::memcpy(imgs_orient.data + ifilter*img_rows*img_cols*imgs_orient.elemSize(),
                filtered_img.data, img_rows*img_cols*imgs_orient.elemSize());
        }

        return imgs_orient;
    }


    // TODO(Pingjun Chen 22:4:2016): Interface now. Need to Improve
    cv::Mat UCM::Contour2UCM(const cv::Mat& imgs_orient, std::string fmt)
    {
        cv::Mat ws_wt = CreateFinestPartition(imgs_orient);
        cv::Mat ws_wt2 = SuperContour4C(ws_wt);
        ws_wt2 = CleanWaterShed(ws_wt2);
        
        ws_wt2.convertTo(ws_wt2, CV_64F);
        cv::Mat labels = CreateConnectedComponent(ws_wt2);
        cv::copyMakeBorder(ws_wt2, ws_wt2, 0, 1, 0, 1, cv::BORDER_REPLICATE);

        cv::Mat super_ucm = UCMMeanPB(ws_wt2, labels);
        cv::Mat norm_ucm = NormalizeImg(super_ucm);
        norm_ucm.convertTo(norm_ucm, CV_32F);

        return norm_ucm; // need to change this
    }

    cv::Mat UCM::CreateFinestPartition(const cv::Mat& imgs_orient)
    {
        // get maximum value in each pixel of stacked imgs_orient
        int img_depth = imgs_orient.size[0];
        int img_rows = imgs_orient.size[1];
        int img_cols = imgs_orient.size[2];

        // create pb and initilize with imgs_orient[0]
        cv::Mat pb = cv::Mat(img_rows, img_cols, CV_32F);
        std::memcpy(pb.data, imgs_orient.data,
            img_rows*img_cols*imgs_orient.elemSize());

        cv::Mat tmp_orient = cv::Mat(img_rows, img_cols, CV_32F);
        for (int iorient = 1; iorient < img_depth; ++iorient)
        {
            std::memcpy(tmp_orient.data,
                imgs_orient.data + iorient*img_rows*img_cols*imgs_orient.elemSize(),
                img_rows*img_cols*imgs_orient.elemSize());
            // compare pb and tmp_orient pixel by pixel, saving max value to pb
            cv::max(pb, tmp_orient, pb);
        }

        int window_size = 1;
        cv::Mat imageu;
        pb.convertTo(imageu, CV_8U, 255);
        // Find the local minima
        cv::Mat marker = FindLocalMinima(imageu, window_size);
        
        // Apply watershed segmentation
        cv::Mat regions = WatershedFull(imageu, marker);
        // cv::Mat regions = ComputeWatershed(pb, marker);

        // Setting the corner part
        int col_num = regions.cols;
        int row_num = regions.rows;
        for (int icol = 0; icol < col_num; ++icol)
        {
            regions.at<int>(0, icol) = regions.at<int>(1, icol);
            regions.at<int>(row_num - 1, icol) = regions.at<int>(row_num - 2, icol);
        }
        for (int irow = 0; irow < row_num; ++irow)
        {
            regions.at<int>(irow, 0) = regions.at<int>(irow, 1);
            regions.at<int>(irow, col_num-1) = regions.at<int>(irow, col_num-2);
        }

        for (int i_row = 0; i_row < pb.rows; ++i_row)
        {
            for (int j_col = 0; j_col < pb.cols; ++j_col)
            {
                if (regions.at<int>(i_row, j_col) != 0)
                    pb.at<float>(i_row, j_col) = 0.0;
            }
        }

        // return regions; // need to change this
        return pb;
    }

    cv::Mat UCM::SuperContour4C(const cv::Mat& pb)
    {
        int pb_rows = pb.rows;
        int pb_cols = pb.cols;
        
        cv::Rect v_up(0, 0, pb_cols, pb_rows-1);
        cv::Rect v_down(0, 1, pb_cols, pb_rows-1);
        cv::Mat v_min = cv::min(pb(v_up), pb(v_down));

        cv::Rect h_up(0, 0, pb_cols-1, pb_rows);
        cv::Rect h_down(1, 0, pb_cols-1, pb_rows);
        cv::Mat h_min = cv::min(pb(h_up).clone(), pb(h_down).clone());
        
        //    pb2 = zeros(2*tx, 2*ty);
        cv::Mat pb2 = cv::Mat::zeros(pb.rows*2, pb.cols*2, CV_32F);
        //    pb2(1:2:end, 1:2:end) = pb;
        for (int irow = 0; irow < pb.rows; ++irow)
        {
            for (int jcol = 0; jcol < pb.cols; ++jcol)
            {
                pb2.at<float>(irow * 2, jcol * 2) = pb.at<float>(irow, jcol);
            }
        }
        //    pb2(1:2:end, 2:2:end-2) = H;
        for (int irow = 0; irow < h_min.rows; ++irow)
        {
            for (int jcol = 0; jcol < h_min.cols; ++jcol)
            {
                pb2.at<float>(irow * 2, jcol * 2 + 1) = h_min.at<float>(irow, jcol);
            }
        }
        //    pb2(2:2:end-2, 1:2:end) = V;
        for (int irow = 0; irow < v_min.rows; ++irow)
        {
            for (int jcol = 0; jcol < v_min.cols; ++jcol)
                pb2.at<float>(irow * 2 + 1, jcol * 2) = v_min.at<float>(irow, jcol);
        }
        //    pb2(end,:) = pb2(end-1, :);
        for (int jcol = 0; jcol < pb2.cols; ++jcol)
        {
            pb2.at<float>(pb2.rows - 1, jcol) = pb2.at<float>(pb2.rows-2, jcol);
        }
        //    pb2(:,end) = max(pb2(:,end), pb2(:,end-1));
        for (int irow = 0; irow < pb2.rows; ++irow)
        {
            pb2.at<float>(irow, pb2.cols-1) = 
                std::max(pb2.at<float>(irow, pb2.cols-1), 
                        pb2.at<float>(irow, pb2.cols-2));
        }

        return pb2;
    }

    cv::Mat UCM::CleanWaterShed(const cv::Mat& ws)
    {
        cv::Mat ws_clean = ws.clone();
        cv::Mat bin_ws = cv::Mat::zeros(ws_clean.rows, ws_clean.cols, CV_8U);
        for (int irow = 0; irow < ws_clean.rows; ++irow)
        {
            for (int jcol = 0; jcol < ws_clean.cols; ++jcol)
            {
                bin_ws.at<uchar>(irow, jcol) =
                    ws_clean.at<float>(irow, jcol) < 1.0e-22 ? 255 : 0;
            }
        }
        cv::Mat bin_clean = MorphClean(bin_ws);

        // cv::findNonZero for R = regionprops(bwlabel(artifacts), 'PixelList');
        cv::Mat artifacts = cv::Mat::zeros(bin_clean.rows, bin_clean.cols, CV_8U);
        // artifacts = ( c==0 & ws_clean==0 );
        cv::bitwise_and(255-bin_clean, bin_ws, artifacts);


        ////// bwlabel(artifacts)
        ////cv::Mat labels;
        ////int num_cc = cv::connectedComponents(artifacts, labels, 8, CV_32S);
        //////    R = regionprops(bwlabel(artifacts), 'PixelList');
        ////cv::Mat locations;
        ////cv::findNonZero(labels > 0, locations);
        ////for (int ipoints = 0; ipoints < locations.total(); ipoints++) 
        ////{
        ////    int row_coor = locations.at<cv::Point>(ipoints).y;
        ////    int col_coor = locations.at<cv::Point>(ipoints).x;
        ////    // add processing like [nd, id] = min(vec);
        ////}
        
        // Todo: to write when necessary
        //std::vector<RegProps> props;
        //RegionProps(artifacts, props, RP_PIXEL_IDX_LIST);

        //int row_num = artifacts.rows;
        //int col_num = artifacts.cols;

        //// traverse all regions
        //for (int ir = 0; ir < props.size(); ++ir)
        //{
        //    // traverse all pixles
        //    for (int ip = 0; ip < props[ir].pixelidxlist.size(); ++ip)
        //    {
        //        int row_coor = props[ir].pixelidxlist[ip] / col_num;
        //        int col_coor = props[ir].pixelidxlist[ip] % col_num;

        //        std::vector<uchar> vec = {
        //            std::max(ws_clean.at<uchar>(row_coor - 2, col_coor - 1), ws_clean.at<uchar>(row_coor - 1, col_coor - 2)),
        //            std::max(ws_clean.at<uchar>(row_coor + 2, col_coor - 1), ws_clean.at<uchar>(row_coor + 1, col_coor - 2)),
        //            std::max(ws_clean.at<uchar>(row_coor + 2, col_coor + 1), ws_clean.at<uchar>(row_coor + 1, col_coor + 2)),
        //            std::max(ws_clean.at<uchar>(row_coor - 2, col_coor + 1), ws_clean.at<uchar>(row_coor - 1, col_coor + 2))
        //            };
        //    }
        //    
        //}

        return ws_clean;
    }

    void UCM::MexContourSides(const cv::Mat& nmax)
    {
        //math::matrices::matrix<> im;
    }

}