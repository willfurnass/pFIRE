#include "basis.hpp"

#include<petscmat.h>

#include "iterator_routines.hpp"
#include "indexing.hpp"

// scalings are src_spacing/tgt_spacing for each dim, offsets are tgt[0,0,0] - src[0,0,0]
Mat_unique build_basis_matrix(
    MPI_Comm comm, const intvector& src_shape, const intvector& tgt_shape, 
    const floatvector& scalings, const floatvector& offsets, integer ndim, integer tile_dim)
{
  intvector src_shape_trunc(ndim, 0);
  std::copy_n(src_shape.begin(), ndim, src_shape_trunc.begin());
  intvector tgt_shape_trunc(ndim, 0);
  std::copy_n(tgt_shape.begin(), ndim, tgt_shape_trunc.begin());

  // get total nodes per dim, and tot_rows = tgt_size*ndim
  integer src_size = std::accumulate(src_shape_trunc.begin(), src_shape_trunc.end(), 1,
                                     std::multiplies<>());
  integer tgt_size = std::accumulate(tgt_shape_trunc.begin(), tgt_shape_trunc.end(), 1,
                                     std::multiplies<>());
  integer m_size = tile_dim * tgt_size;
  integer n_size = tile_dim * src_size;

  integer rank, num_ranks;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &num_ranks);

  integer rowsize = m_size / num_ranks;
  integer remainder = m_size % num_ranks;
  integer startrow = rowsize * rank;
  // take care of remainder
  if(rank < remainder){
    rowsize += 1;
    startrow += rank;
  } else {
    startrow += remainder;
  }
  integer endrow = startrow + rowsize;
  // construct CSR format directly
  intvector idxn, idxm;
  floatvector mdat;
  integer rowptr = 0;
  idxn.push_back(rowptr);

  integer npoints = 1;
  for(integer idim=0; idim<ndim; idim++)
  {
    npoints *= 2;
  }
  
  for(integer gidx=startrow; gidx<endrow; gidx++)
  {
    integer idx = gidx%tgt_size;
    integer col_ofs = src_size * (gidx/tgt_size);
    // unravel tgt loc and find equivalent source loc
    intvector tgt_coord = unravel(idx, tgt_shape_trunc);
    floatvector src_coord(ndim, 0.);
    intvector src_coord_floor(ndim, 0);
    // need both exact loc and floored loc
    n_ary_transform([](floating x, floating a, floating b) -> floating{return a*x + b;},
		       src_coord.begin(), tgt_coord.begin(), tgt_coord.end(),
                       scalings.begin(), offsets.begin());
    std::transform(src_coord.begin(), src_coord.end(), src_coord_floor.begin(),
                   [](floating x) -> integer{return static_cast<integer> (std::floor(x));});


    for(integer ipoint=0; ipoint<npoints; ipoint++){
      for(integer idim=0; idim<ndim; idim++){
        if((1 << idim) & ipoint)
        {
          src_coord_floor[idim] += 1;
        }
      }
      if(all_true_varlen(src_coord_floor.begin(), src_coord_floor.end(), src_shape_trunc.begin(),
                         src_shape_trunc.end(), std::less<>()) 
          && std::all_of(src_coord_floor.begin(), src_coord_floor.end(),
                         [](floating a) -> bool{return a >= 0;}))
      {
        floating coeff = calculate_basis_coefficient(src_coord.begin(), src_coord.end(),
                                                     src_coord_floor.begin());
        if(coeff > 0)
        {
          rowptr++;
          idxm.push_back(ravel(src_coord_floor, src_shape_trunc) + col_ofs);
          mdat.push_back(coeff);
        }
      }
      for(integer idim=0; idim<ndim; idim++){
        if((1 << idim) & ipoint)
        {
          src_coord_floor[idim] -= 1;
        }
      }
    }
    idxn.push_back(rowptr);
  }
  Mat_unique m_basis = create_unique_mat();
  PetscErrorCode perr = MatCreateMPIAIJWithArrays(
      comm, idxn.size()-1, PETSC_DECIDE, m_size, n_size, idxn.data(), idxm.data(),
      mdat.data(), m_basis.get());CHKERRABORT(comm, perr);

  return m_basis;
}

Mat_unique build_warp_matrix(MPI_Comm comm, const intvector& img_shape, integer ndim,
                             const std::vector<Vec*>& displacements)
{
  // get total nodes per dim, and tot_rows = tgt_size*ndim
  integer mat_size = std::accumulate(img_shape.begin(), img_shape.end(), 1,
                                     std::multiplies<>());
  if(img_shape.size() < ndim)
  {
    throw std::runtime_error("image dimensions must match ndim");
  }

  if(displacements.size() < ndim)
  {
    throw std::runtime_error("must have displacement vector for each image dimension");
  }

  intvector img_shape_trunc(ndim, 0);
  std::copy_n(img_shape.begin(), ndim, img_shape_trunc.begin());

  // TODO: should really be asserting all displacement vectors are the right shape and layout
  integer startrow, endrow;
  PetscErrorCode perr = VecGetOwnershipRange(*displacements[0], &startrow, &endrow);

  // construct CSR format directly
  intvector idxn, idxm;
  floatvector mdat;
  integer rowptr = 0;
  idxn.push_back(rowptr);

  integer npoints = 1;
  for(integer idim=0; idim<ndim; idim++)
  {
    npoints *= 2;
  }
  
  std::vector<floating*> raw_arrs(ndim, nullptr);
  // lambda needed here anyway to capture comm
  auto get_raw_array = [comm](floating*& a, const Vec* v) -> void
    {PetscErrorCode p = VecGetArray(*v, &a);CHKERRABORT(comm, p);};
  n_ary_for_each(get_raw_array, raw_arrs.begin(), raw_arrs.end(), displacements.begin());

  for(integer idx=startrow; idx<endrow; idx++)
  {
    // unravel tgt loc and find equivalent source loc
    integer locidx = idx - startrow;
    intvector tgt_coord = unravel(idx, img_shape_trunc);
    floatvector src_coord(ndim, 0.);
    intvector src_coord_floor(ndim, 0);

    // offset src_coord
    std::transform(tgt_coord.begin(), tgt_coord.end(), raw_arrs.begin(), src_coord.begin(),
                   [locidx](floating x, floating* arr) -> floating{return x + arr[locidx];});
    // Need to clamp locations to the edges of the image
    std::transform(src_coord.begin(), src_coord.end(), img_shape_trunc.begin(), src_coord.begin(),
                   clamp);
    // Floor to get nearest pixel
    std::transform(src_coord.begin(), src_coord.end(), src_coord_floor.begin(),
                   [](floating x) -> integer{return static_cast<integer> (std::floor(x));});

    for(integer ipoint=0; ipoint<npoints; ipoint++){
      for(integer idim=0; idim<ndim; idim++){
        if((1 << idim) & ipoint)
        {
          src_coord_floor[idim] += 1;
        }
      }
      if(all_true_varlen(src_coord_floor.begin(), src_coord_floor.end(), img_shape_trunc.begin(),
                         img_shape_trunc.end(), std::less<>()) 
          && std::all_of(src_coord_floor.begin(), src_coord_floor.end(),
                         [](floating a) -> bool{return a >= 0;}))
      {
        floating coeff = calculate_basis_coefficient(src_coord.begin(), src_coord.end(),
                                                     src_coord_floor.begin());
        if(coeff > 0)
        {
          rowptr++;
          idxm.push_back(ravel(src_coord_floor, img_shape_trunc));
          mdat.push_back(coeff);
        }
      }
      for(integer idim=0; idim<ndim; idim++){
        if((1 << idim) & ipoint)
        {
          src_coord_floor[idim] -= 1;
        }
      }
    }
    idxn.push_back(rowptr);
  }
  // lambda needed here  anyway to capture comm
  auto restore_raw_array = [comm](floating*& a, const Vec* v) -> void
    {PetscErrorCode p = VecRestoreArray(*v, &a);CHKERRABORT(comm, p);};
  n_ary_for_each(restore_raw_array, raw_arrs.begin(), raw_arrs.end(), displacements.begin());

  Mat_unique warp = create_unique_mat();
  perr = MatCreateMPIAIJWithArrays(
      comm, idxn.size()-1, idxn.size()-1, mat_size, mat_size, idxn.data(), idxm.data(),
      mdat.data(), warp.get());CHKERRABORT(comm, perr);

  return warp;
}
