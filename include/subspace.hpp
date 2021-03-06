/*
 * Copyright (c) 2011. Philipp Wagner <bytefish[at]gmx[dot]de>.
 * Released to public domain under terms of the BSD Simplified license.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the organization nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 *   See <http://www.opensource.org/licenses/bsd-license>
 */

#ifndef __SUBSPACE_HPP__
#define __SUBSPACE_HPP__

#include "opencv2/opencv.hpp"
#include "helper.hpp"
#include "decomposition.hpp"


using namespace cv;
using namespace std;

namespace subspace {

//! project samples into W
inline Mat project(const Mat& W, const Mat& mean, const Mat& src) {
    // temporary matrices
    Mat X, Y;
    // copy data & make sure we are using the correct type
    src.convertTo(X, W.type());
    // get number of samples and dimension
    int n = X.rows;
    int d = X.cols;
    // center the data if sample mean is given
    if(mean.total() == d)
        subtract(X, repeat(mean.reshape(1,1), n, 1), X);
    // calculate projection as Y = (X-mean)*W
    gemm(X, W, 1.0, Mat(), 0.0, Y);
    return Y;
}

//! reconstruct samples from W
inline Mat reconstruct(const Mat& W, const Mat& mean, const Mat& src) {
    // get number of samples and dimension
    int n = src.rows;
    int d = src.cols;
    // initalize temporary matrices
    Mat X, Y;
    // copy data & make sure we are using the correct type
    src.convertTo(Y, W.type());
    // calculate the reconstruction X = Y*W + mean
    gemm(Y,
            W,
            1.0,
            (d == mean.total()) ? repeat(mean.reshape(1,1), n, 1) : Mat(),
            (d == mean.total()) ? 1.0 : 0.0,
            X,
            GEMM_2_T);
    return X;
}

//! Performs a Linear Discriminant Analysis
class LDA {

private:
    bool _dataAsRow;
    int _num_components;
    Mat _eigenvectors;
    Mat _eigenvalues;

public:

    // Initializes a LDA with num_components (default 0) and specifies how
    // samples are aligned (default dataAsRow=true).
    LDA(int num_components = 0, bool dataAsRow = true) :
        _num_components(num_components),
        _dataAsRow(dataAsRow) {};

    // Initializes and performs a Discriminant Analysis with Fisher's
    // Optimization Criterion on given data in src and corresponding labels
    // in labels. If 0 (or less) number of components are given, they are
    // automatically determined for given data in computation.
    LDA(const Mat& src, const vector<int>& labels,
            int num_components = 0,
            bool dataAsRow = true) :
                _num_components(num_components),
                _dataAsRow(dataAsRow)
    {
        this->compute(src, labels); //! compute eigenvectors and eigenvalues
    }

    // Initializes and performs a Discriminant Analysis with Fisher's
    // Optimization Criterion on given data in src and corresponding labels
    // in labels. If 0 (or less) number of components are given, they are
    // automatically determined for given data in computation.
    LDA(const vector<Mat>& src,
            const vector<int>& labels,
            int num_components = 0,
            bool dataAsRow = true) :
                _num_components(num_components),
                _dataAsRow(dataAsRow)
    {
        this->compute(src, labels); //! compute eigenvectors and eigenvalues
    }

    // Destructor.
    ~LDA() {}

    //! compute the discriminants for data in src and labels
    void compute(const Mat& src, const vector<int>& labels) {
        if(src.channels() != 1)
            CV_Error(CV_StsBadArg, "Only single channel matrices allowed.");
        Mat data = _dataAsRow ? src.clone() : transpose(src);
        // ensures internal data is double
        data.convertTo(data, CV_64FC1);
        // maps the labels, so they're ascending: [0,1,...,C]
        vector<int> mapped_labels(labels.size());
        vector<int> num2label = remove_dups(labels);
        map<int,int> label2num;
        for(int i=0;i<num2label.size();i++)
            label2num[num2label[i]] = i;
        for(int i=0;i<labels.size();i++)
            mapped_labels[i] = label2num[labels[i]];
        // get sample size, dimension
        int N = data.rows;
        int D = data.cols;
        // number of unique labels
        int C = num2label.size();
        // throw error if less labels, than samples
        if(labels.size() != N)
            CV_Error(CV_StsBadArg, "Error: The number of samples must equal the number of labels.");
        // warn if within-classes scatter matrix becomes singular
        if(N < D)
            cout << "Warning: Less observations than feature dimension given! Computation will probably fail." << endl;
        // clip number of components to be a valid number
        if((_num_components <= 0) || (_num_components > (C-1)))
            _num_components = (C-1);
        // holds the mean over all classes
        Mat meanTotal = Mat::zeros(1, D, data.type());
        // holds the mean for each class
        Mat meanClass[C];
        int numClass[C];
        // initialize
        for (int i = 0; i < C; i++) {
            numClass[i] = 0;
            meanClass[i] = Mat::zeros(1, D, data.type()); //! Dx1 image vector
        }
        // calculate sums
        for (int i = 0; i < N; i++) {
            Mat instance = data.row(i);
            int classIdx = mapped_labels[i];
            add(meanTotal, instance, meanTotal);
            add(meanClass[classIdx], instance, meanClass[classIdx]);
            numClass[classIdx]++;
        }
        // calculate means
        meanTotal.convertTo(meanTotal, meanTotal.type(), 1.0/static_cast<double>(N));
        for (int i = 0; i < C; i++)
            meanClass[i].convertTo(meanClass[i], meanClass[i].type(), 1.0/static_cast<double>(numClass[i]));
        // subtract class means
        for (int i = 0; i < N; i++) {
            int classIdx = mapped_labels[i];
            Mat instance = data.row(i);
            subtract(instance, meanClass[classIdx], instance);
        }
        // calculate within-classes scatter
        Mat Sw = Mat::zeros(D, D, data.type());
        mulTransposed(data, Sw, true);
        // calculate between-classes scatter
        Mat Sb = Mat::zeros(D, D, data.type());
        for (int i = 0; i < C; i++) {
            Mat tmp;
            subtract(meanClass[i], meanTotal, tmp);
            mulTransposed(tmp, tmp, true);
            add(Sb, tmp, Sb);
        }
        // invert Sw
        Mat Swi = Sw.inv();
        // M = inv(Sw)*Sb
        Mat M;
        gemm(Swi, Sb, 1.0, Mat(), 0.0, M);
        EigenvalueDecomposition es(M);
        _eigenvalues = es.eigenvalues();
        _eigenvectors = es.eigenvectors();
        // reshape eigenvalues, so they are stored by column
        _eigenvalues = _eigenvalues.reshape(1,1);
        // get sorted indices descending by their eigenvalue
        vector<int> sorted_indices = argsort(_eigenvalues, false);
        // now sort eigenvalues and eigenvectors accordingly
        _eigenvalues = sortMatrixByColumn(_eigenvalues, sorted_indices);
        _eigenvectors = sortMatrixByColumn(_eigenvectors, sorted_indices);
        // and now take only the num_components and we're out!
        _eigenvalues = Mat(_eigenvalues, Range::all(), Range(0,_num_components));
        _eigenvectors = Mat(_eigenvectors, Range::all(), Range(0, _num_components));
    }

    // Computes the discriminants for data in src and corresponding labels in labels.
    void compute(const vector<Mat>& src, const vector<int>& labels) {
        compute(_dataAsRow ? asRowMatrix(src, CV_64FC1) : asColumnMatrix(src, CV_64FC1), labels);
    }

    // Projects samples into the LDA subspace.
    Mat project(const Mat& src) {
        return subspace::project(_eigenvectors, Mat(), _dataAsRow ? src : transpose(src));
    }

    // Reconstructs projections from the LDA subspace.
    Mat reconstruct(const Mat& src) {
        return subspace::reconstruct(_eigenvectors, Mat(), _dataAsRow ? src : transpose(src));
    }
    // Returns the eigenvectors of this LDA.
    Mat eigenvectors() const { return _eigenvectors; };

    // Returns the eigenvalues of this LDA.
    Mat eigenvalues() const { return _eigenvalues; }
};

} // namespace
#endif
